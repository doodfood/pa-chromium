// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mdns_client_impl.h"

#include "base/bind.h"
#include "base/message_loop_proxy.h"
#include "base/stl_util.h"
#include "base/time/default_clock.h"
#include "net/base/dns_util.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/rand_callback.h"
#include "net/dns/dns_protocol.h"
#include "net/udp/datagram_socket.h"

namespace net {

namespace {
const char kMDnsMulticastGroupIPv4[] = "224.0.0.251";
const char kMDnsMulticastGroupIPv6[] = "FF02::FB";
const unsigned MDnsTransactionTimeoutSeconds = 3;
}

MDnsConnection::SocketHandler::SocketHandler(
    MDnsConnection* connection, const IPEndPoint& multicast_addr,
    MDnsConnection::SocketFactory* socket_factory)
    : socket_(socket_factory->CreateSocket()), connection_(connection),
      response_(new DnsResponse(dns_protocol::kMaxMulticastSize)),
      multicast_addr_(multicast_addr) {
}

MDnsConnection::SocketHandler::~SocketHandler() {
}

int MDnsConnection::SocketHandler::Start() {
  int rv = BindSocket();
  if (rv != OK) {
    return rv;
  }

  return DoLoop(0);
}

int MDnsConnection::SocketHandler::DoLoop(int rv) {
  do {
    if (rv > 0)
      connection_->OnDatagramReceived(response_.get(), recv_addr_, rv);

    rv = socket_->RecvFrom(
        response_->io_buffer(),
        response_->io_buffer()->size(),
        &recv_addr_,
        base::Bind(&MDnsConnection::SocketHandler::OnDatagramReceived,
                   base::Unretained(this)));
  } while (rv > 0);

  if (rv != ERR_IO_PENDING)
    return rv;

  return OK;
}

void MDnsConnection::SocketHandler::OnDatagramReceived(int rv) {
  if (rv >= OK)
    rv = DoLoop(rv);

  if (rv != OK)
    connection_->OnError(this, rv);
}

int MDnsConnection::SocketHandler::Send(IOBuffer* buffer, unsigned size) {
  return socket_->SendTo(
      buffer, size, multicast_addr_,
      base::Bind(&MDnsConnection::SocketHandler::SendDone,
                 base::Unretained(this) ));
}

void MDnsConnection::SocketHandler::SendDone(int rv) {
  // TODO(noamsml): Retry logic.
}

int MDnsConnection::SocketHandler::BindSocket() {
  IPAddressNumber address_any(multicast_addr_.address().size());

  IPEndPoint bind_endpoint(address_any, multicast_addr_.port());

  socket_->AllowAddressReuse();
  int rv = socket_->Listen(bind_endpoint);

  if (rv < OK) return rv;

  socket_->SetMulticastLoopbackMode(false);

  return socket_->JoinGroup(multicast_addr_.address());
}

MDnsConnection::MDnsConnection(MDnsConnection::SocketFactory* socket_factory,
                               MDnsConnection::Delegate* delegate)
    : socket_handler_ipv4_(this,
                           GetMDnsIPEndPoint(kMDnsMulticastGroupIPv4),
                           socket_factory),
      socket_handler_ipv6_(this,
                           GetMDnsIPEndPoint(kMDnsMulticastGroupIPv6),
                           socket_factory),
      delegate_(delegate) {
}

MDnsConnection::~MDnsConnection() {
}

int MDnsConnection::Init() {
  int rv;

  rv = socket_handler_ipv4_.Start();
  if (rv != OK) return rv;
  rv = socket_handler_ipv6_.Start();
  if (rv != OK) return rv;

  return OK;
}

int MDnsConnection::Send(IOBuffer* buffer, unsigned size) {
  int rv;

  rv = socket_handler_ipv4_.Send(buffer, size);
  if (rv < OK && rv != ERR_IO_PENDING) return rv;

  rv = socket_handler_ipv6_.Send(buffer, size);
  if (rv < OK && rv != ERR_IO_PENDING) return rv;

  return OK;
}

void MDnsConnection::OnError(SocketHandler* loop,
                             int error) {
  // TODO(noamsml): Specific handling of intermittent errors that can be handled
  // in the connection.
  delegate_->OnConnectionError(error);
}

IPEndPoint MDnsConnection::GetMDnsIPEndPoint(const char* address) {
  IPAddressNumber multicast_group_number;
  bool success = ParseIPLiteralToNumber(address,
                                        &multicast_group_number);
  DCHECK(success);
  return IPEndPoint(multicast_group_number,
                    dns_protocol::kDefaultPortMulticast);
}

void MDnsConnection::OnDatagramReceived(
    DnsResponse* response,
    const IPEndPoint& recv_addr,
    int bytes_read) {
  // TODO(noamsml): More sophisticated error handling.
  DCHECK_GT(bytes_read, 0);
  delegate_->HandlePacket(response, bytes_read);
}

class MDnsConnectionSocketFactoryImpl
    : public MDnsConnection::SocketFactory {
 public:
  MDnsConnectionSocketFactoryImpl();
  virtual ~MDnsConnectionSocketFactoryImpl();

  virtual scoped_ptr<DatagramServerSocket> CreateSocket() OVERRIDE;
};

MDnsConnectionSocketFactoryImpl::MDnsConnectionSocketFactoryImpl() {
}

MDnsConnectionSocketFactoryImpl::~MDnsConnectionSocketFactoryImpl() {
}

scoped_ptr<DatagramServerSocket>
MDnsConnectionSocketFactoryImpl::CreateSocket() {
  return scoped_ptr<DatagramServerSocket>(new UDPServerSocket(
      NULL, NetLog::Source()));
}

// static
scoped_ptr<MDnsConnection::SocketFactory>
MDnsConnection::SocketFactory::CreateDefault() {
  return scoped_ptr<MDnsConnection::SocketFactory>(
      new MDnsConnectionSocketFactoryImpl);
}

MDnsClientImpl::Core::Core(MDnsClientImpl* client,
                           MDnsConnection::SocketFactory* socket_factory)
    : client_(client), connection_(new MDnsConnection(socket_factory, this)) {
}

MDnsClientImpl::Core::~Core() {
  STLDeleteValues(&listeners_);
}

bool MDnsClientImpl::Core::Init() {
  return connection_->Init() == OK;
}

bool MDnsClientImpl::Core::SendQuery(uint16 rrtype, std::string name) {
  std::string name_dns;
  if (!DNSDomainFromDot(name, &name_dns))
    return false;

  DnsQuery query(0, name_dns, rrtype);
  query.set_flags(0);  // Remove the RD flag from the query. It is unneeded.

  return connection_->Send(query.io_buffer(), query.io_buffer()->size()) == OK;
}

void MDnsClientImpl::Core::HandlePacket(DnsResponse* response,
                                        int bytes_read) {
  unsigned offset;

  if (!response->InitParseWithoutQuery(bytes_read)) {
    LOG(WARNING) << "Could not understand an mDNS packet.";
    return;  // Message is unreadable.
  }

  // TODO(noamsml): duplicate query suppression.
  if (!(response->flags() & dns_protocol::kFlagResponse))
    return;  // Message is a query. ignore it.

  DnsRecordParser parser = response->Parser();
  unsigned answer_count = response->answer_count() +
      response->additional_answer_count();

  for (unsigned i = 0; i < answer_count; i++) {
    offset = parser.GetOffset();
    scoped_ptr<const RecordParsed> scoped_record = RecordParsed::CreateFrom(
        &parser, base::Time::Now());

    if (!scoped_record) {
      LOG(WARNING) << "Could not understand an mDNS record.";

      if (offset == parser.GetOffset()) {
        LOG(WARNING) << "Abandoned parsing the rest of the packet.";
        return;  // The parser did not advance, abort reading the packet.
      } else {
        continue;  // We may be able to extract other records from the packet.
      }
    }

    if ((scoped_record->klass() & dns_protocol::kMDnsClassMask) !=
        dns_protocol::kClassIN) {
      LOG(WARNING) << "Received an mDNS record with non-IN class. Ignoring.";
      continue;  // Ignore all records not in the IN class.
    }

    // We want to retain a copy of the record pointer for updating listeners
    // but we are passing ownership to the cache.
    const RecordParsed* record = scoped_record.get();
    MDnsCache::UpdateType update = cache_.UpdateDnsRecord(scoped_record.Pass());

    // Cleanup time may have changed.
    ScheduleCleanup(cache_.next_expiration());

    if (update != MDnsCache::NoChange) {
      MDnsListener::UpdateType update_external;

      switch (update) {
        case MDnsCache::RecordAdded:
          update_external = MDnsListener::RECORD_ADDED;
          break;
        case MDnsCache::RecordChanged:
          update_external = MDnsListener::RECORD_CHANGED;
          break;
        case MDnsCache::NoChange:
        default:
          NOTREACHED();
          // Dummy assignment to suppress compiler warning.
          update_external = MDnsListener::RECORD_CHANGED;
          break;
      }

      AlertListeners(update_external,
                     ListenerKey(record->type(), record->name()), record);
      // Alert listeners listening only for rrtype and not for name.
      AlertListeners(update_external, ListenerKey(record->type(), ""), record);
    }
  }
}

void MDnsClientImpl::Core::OnConnectionError(int error) {
  // TODO(noamsml): On connection error, recreate connection and flush cache.
}

void MDnsClientImpl::Core::AlertListeners(
    MDnsListener::UpdateType update_type,
    const ListenerKey& key,
    const RecordParsed* record) {
  ListenerMap::iterator listener_map_iterator = listeners_.find(key);
  if (listener_map_iterator == listeners_.end()) return;

  FOR_EACH_OBSERVER(MDnsListenerImpl, *listener_map_iterator->second,
                    AlertDelegate(update_type, record));
}

void MDnsClientImpl::Core::AddListener(
    MDnsListenerImpl* listener) {
  ListenerKey key(listener->GetType(), listener->GetName());
  std::pair<ListenerMap::iterator, bool> observer_insert_result =
      listeners_.insert(
          make_pair(key, static_cast<ObserverList<MDnsListenerImpl>*>(NULL)));

  // If an equivalent key does not exist, actually create the observer list.
  if (observer_insert_result.second)
    observer_insert_result.first->second = new ObserverList<MDnsListenerImpl>();

  ObserverList<MDnsListenerImpl>* observer_list =
      observer_insert_result.first->second;

  observer_list->AddObserver(listener);
}

void MDnsClientImpl::Core::RemoveListener(MDnsListenerImpl* listener) {
  ListenerKey key(listener->GetType(), listener->GetName());
  ListenerMap::iterator observer_list_iterator = listeners_.find(key);

  DCHECK(observer_list_iterator != listeners_.end());
  DCHECK(observer_list_iterator->second->HasObserver(listener));

  observer_list_iterator->second->RemoveObserver(listener);

  // Remove the observer list from the map if it is empty
  if (observer_list_iterator->second->size() == 0) {
    delete observer_list_iterator->second;
    listeners_.erase(observer_list_iterator);
  }
}

void MDnsClientImpl::Core::ScheduleCleanup(base::Time cleanup) {
  // Cleanup is already scheduled, no need to do anything.
  if (cleanup == scheduled_cleanup_) return;
  scheduled_cleanup_ = cleanup;

  // This cancels the previously scheduled cleanup.
  cleanup_callback_.Reset(base::Bind(
      &MDnsClientImpl::Core::DoCleanup, base::Unretained(this)));

  // If |cleanup| is empty, then no cleanup necessary.
  if (cleanup != base::Time()) {
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        cleanup_callback_.callback(),
        cleanup - base::Time::Now());
  }
}

void MDnsClientImpl::Core::DoCleanup() {
  cache_.CleanupRecords(base::Time::Now(), base::Bind(
      &MDnsClientImpl::Core::OnRecordRemoved, base::Unretained(this)));

  ScheduleCleanup(cache_.next_expiration());
}

void MDnsClientImpl::Core::OnRecordRemoved(
    const RecordParsed* record) {
  AlertListeners(MDnsListener::RECORD_REMOVED,
                 ListenerKey(record->type(), record->name()), record);
  // Alert listeners listening only for rrtype and not for name.
  AlertListeners(MDnsListener::RECORD_REMOVED, ListenerKey(record->type(), ""),
                 record);
}

void MDnsClientImpl::Core::QueryCache(
    uint16 rrtype, const std::string& name,
    std::vector<const RecordParsed*>* records) const {
  cache_.FindDnsRecords(rrtype, name, records, base::Time::Now());
}

MDnsClientImpl::MDnsClientImpl(
    scoped_ptr<MDnsConnection::SocketFactory> socket_factory)
    : listen_refs_(0), socket_factory_(socket_factory.Pass()) {
}

MDnsClientImpl::~MDnsClientImpl() {
}

bool MDnsClientImpl::AddListenRef() {
  if (!core_.get()) {
    core_.reset(new Core(this, socket_factory_.get()));
    if (!core_->Init()) {
      core_.reset();
      return false;
    }
  }
  listen_refs_++;
  return true;
}

void MDnsClientImpl::SubtractListenRef() {
  listen_refs_--;
  if (listen_refs_ == 0) {
    base::MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
        &MDnsClientImpl::Shutdown, base::Unretained(this)));
  }
}

void MDnsClientImpl::Shutdown() {
  // We need to check that new listeners haven't been created.
  if (listen_refs_ == 0) {
    core_.reset();
  }
}

bool MDnsClientImpl::IsListeningForTests() {
  return core_.get() != NULL;
}

scoped_ptr<MDnsListener> MDnsClientImpl::CreateListener(
    uint16 rrtype,
    const std::string& name,
    MDnsListener::Delegate* delegate) {
  return scoped_ptr<net::MDnsListener>(
      new MDnsListenerImpl(rrtype, name, delegate, this));
}

scoped_ptr<MDnsTransaction> MDnsClientImpl::CreateTransaction(
    uint16 rrtype,
    const std::string& name,
    int flags,
    const MDnsTransaction::ResultCallback& callback) {
  return scoped_ptr<MDnsTransaction>(
      new MDnsTransactionImpl(rrtype, name, flags, callback, this));
}

MDnsListenerImpl::MDnsListenerImpl(
    uint16 rrtype,
    const std::string& name,
    MDnsListener::Delegate* delegate,
    MDnsClientImpl* client)
    : rrtype_(rrtype), name_(name), client_(client), delegate_(delegate),
      started_(false) {
}

bool MDnsListenerImpl::Start() {
  DCHECK(!started_);

  if (!client_->AddListenRef()) return false;
  started_ = true;

  DCHECK(client_->core());
  client_->core()->AddListener(this);

  return true;
}

MDnsListenerImpl::~MDnsListenerImpl() {
  if (started_) {
    DCHECK(client_->core());
    client_->core()->RemoveListener(this);
    client_->SubtractListenRef();
  }
}

const std::string& MDnsListenerImpl::GetName() const {
  return name_;
}

uint16 MDnsListenerImpl::GetType() const {
  return rrtype_;
}

void MDnsListenerImpl::AlertDelegate(MDnsListener::UpdateType update_type,
                                     const RecordParsed* record) {
  DCHECK(started_);
  delegate_->OnRecordUpdate(update_type, record);
}

MDnsTransactionImpl::MDnsTransactionImpl(
    uint16 rrtype,
    const std::string& name,
    int flags,
    const MDnsTransaction::ResultCallback& callback,
    MDnsClientImpl* client)
    : rrtype_(rrtype), name_(name), callback_(callback), client_(client),
      started_(false), flags_(flags) {
  DCHECK((flags_ & MDnsTransaction::FLAG_MASK) == flags_);
  DCHECK(flags_ & MDnsTransaction::QUERY_CACHE ||
         flags_ & MDnsTransaction::QUERY_NETWORK);
}

MDnsTransactionImpl::~MDnsTransactionImpl() {
  timeout_.Cancel();
}

bool MDnsTransactionImpl::Start() {
  DCHECK(!started_);
  started_ = true;
  std::vector<const RecordParsed*> records;
  base::WeakPtr<MDnsTransactionImpl> weak_this = AsWeakPtr();

  if (flags_ & MDnsTransaction::QUERY_CACHE) {
    if (client_->core()) {
      client_->core()->QueryCache(rrtype_, name_, &records);
      for (std::vector<const RecordParsed*>::iterator i = records.begin();
           i != records.end() && weak_this; ++i) {
        weak_this->TriggerCallback(MDnsTransaction::RESULT_RECORD,
                                   records.front());
      }
    }
  }

  if (!weak_this) return true;

  if (is_active() && (flags_ & MDnsTransaction::QUERY_NETWORK)) {
    listener_ = client_->CreateListener(rrtype_, name_, this);
    if (!listener_->Start()) return false;

    DCHECK(client_->core());
    if (!client_->core()->SendQuery(rrtype_, name_))
      return false;

    timeout_.Reset(base::Bind(&MDnsTransactionImpl::SignalTransactionOver,
                              weak_this));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        timeout_.callback(),
        base::TimeDelta::FromSeconds(MDnsTransactionTimeoutSeconds));

    return listener_.get() != NULL;
  } else {
    // If this is a cache only query, signal that the transaction is over
    // immediately.
    SignalTransactionOver();
  }

  return true;
}

const std::string& MDnsTransactionImpl::GetName() const {
  return name_;
}

uint16 MDnsTransactionImpl::GetType() const {
  return rrtype_;
}

void MDnsTransactionImpl::CacheRecordFound(const RecordParsed* record) {
  DCHECK(started_);
  OnRecordUpdate(MDnsListener::RECORD_ADDED, record);
}

void MDnsTransactionImpl::TriggerCallback(MDnsTransaction::Result result,
                                          const RecordParsed* record) {
  DCHECK(started_);
  if (!is_active()) return;

  // Ensure callback is run after touching all class state, so that
  // the callback can delete the transaction.
  MDnsTransaction::ResultCallback callback = callback_;

  if (flags_ & MDnsTransaction::SINGLE_RESULT)
    Reset();

  callback.Run(result, record);
}

void MDnsTransactionImpl::Reset() {
  callback_.Reset();
  listener_.reset();
  timeout_.Cancel();
}

void MDnsTransactionImpl::OnRecordUpdate(MDnsListener::UpdateType update,
                                         const RecordParsed* record) {
  DCHECK(started_);
  if (update ==  MDnsListener::RECORD_ADDED ||
      update == MDnsListener::RECORD_CHANGED)
    TriggerCallback(MDnsTransaction::RESULT_RECORD, record);
}

void MDnsTransactionImpl::SignalTransactionOver() {
  DCHECK(started_);
  base::WeakPtr<MDnsTransactionImpl> weak_this = AsWeakPtr();

  if (flags_ & MDnsTransaction::SINGLE_RESULT) {
    TriggerCallback(MDnsTransaction::RESULT_NO_RESULTS, NULL);
  } else {
    TriggerCallback(MDnsTransaction::RESULT_DONE, NULL);
  }

  if (weak_this) {
    weak_this->Reset();
  }
}

void MDnsTransactionImpl::OnNsecRecord(const std::string& name, unsigned type) {
  // TODO(noamsml): NSEC records not yet implemented
}

void MDnsTransactionImpl::OnCachePurged() {
  // TODO(noamsml): Cache purge situations not yet implemented
}

}  // namespace net
