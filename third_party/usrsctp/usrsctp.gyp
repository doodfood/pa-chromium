# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'libsctp_target_type%': 'static_library',
  },
  'target_defaults': {
    'defines': [
      'INET',
      'SCTP_PROCESS_LEVEL_LOCKS',
      'SCTP_SIMPLE_ALLOCATOR',
      '__Userspace__',
      # 'SCTP_DEBUG', # Uncomment for SCTP debugging.
    ],
    'include_dirs': [
      'overrides/usrsctplib',
      'overrides/usrsctplib/netinet',
      'usrsctplib/',
      'usrsctplib/netinet',
      'usrsctplib/netinet6',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
        'overrides/usrsctplib',
        'overrides/usrsctplib/netinet',
        'usrsctplib/',
        'usrsctplib/netinet',
        'usrsctplib/netinet6',
      ],
    },
    'conditions': [
      ['use_openssl==1', {
        'defines': [
          'SCTP_USE_OPENSSL_SHA1',
        ],
        'dependencies': [
          '../../third_party/openssl/openssl.gyp:openssl',
        ],
      },
      {  # else use_openssl==0, use NSS.
        'defines' : [
          'SCTP_USE_NSS_SHA1',
        ],
        'conditions': [
          ['os_posix == 1 and OS != "mac" and OS != "ios" and OS != "android"', {
            'dependencies': [
              '<(DEPTH)/build/linux/system.gyp:ssl',
            ],
          }],
          ['OS == "mac" or OS == "ios" or OS == "win"', {
            'dependencies': [
              '<(DEPTH)/third_party/nss/nss.gyp:nspr',
              '<(DEPTH)/third_party/nss/nss.gyp:nss',
            ],
          }],
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'usrsctplib',
      'type': 'static_library',
      'sources': [
        'overrides/usrsctplib/netinet/sctp_auth.h',
        'overrides/usrsctplib/netinet/sctp_os.h',
        'overrides/usrsctplib/netinet/sctp_os_userspace.h',
        'overrides/usrsctplib/netinet/sctp_nss_sha1.c',
        'overrides/usrsctplib/netinet/sctp_nss_sha1.h',

        'usrsctplib/usrsctp.h',
        'usrsctplib/user_atomic.h',
        'usrsctplib/user_environment.c',
        'usrsctplib/user_environment.h',
        'usrsctplib/user_inpcb.h',
        'usrsctplib/user_ip6_var.h',
        'usrsctplib/user_ip_icmp.h',
        'usrsctplib/user_mbuf.c',
        'usrsctplib/user_mbuf.h',
        'usrsctplib/user_queue.h',
        'usrsctplib/user_recv_thread.c',
        'usrsctplib/user_recv_thread.h',
        'usrsctplib/user_route.h',
        'usrsctplib/user_sctp_timer_iterate.c',
        'usrsctplib/user_socket.c',
        'usrsctplib/user_socketvar.h',
        'usrsctplib/user_uma.h',
        'usrsctplib/netinet/sctp_asconf.c',
        'usrsctplib/netinet/sctp_asconf.h',
        'usrsctplib/netinet/sctp_auth.c',
        'usrsctplib/netinet/sctp_bsd_addr.c',
        'usrsctplib/netinet/sctp_bsd_addr.h',
        'usrsctplib/netinet/sctp_callout.c',
        'usrsctplib/netinet/sctp_callout.h',
        'usrsctplib/netinet/sctp_cc_functions.c',
        'usrsctplib/netinet/sctp_constants.h',
        'usrsctplib/netinet/sctp_crc32.c',
        'usrsctplib/netinet/sctp_crc32.h',
        'usrsctplib/netinet/sctp_hashdriver.h',
        'usrsctplib/netinet/sctp_hashdriver.c',
        'usrsctplib/netinet/sctp_header.h',
        'usrsctplib/netinet/sctp_indata.c',
        'usrsctplib/netinet/sctp_indata.h',
        'usrsctplib/netinet/sctp_input.c',
        'usrsctplib/netinet/sctp_input.h',
        'usrsctplib/netinet/sctp_lock_userspace.h',
        'usrsctplib/netinet/sctp_output.c',
        'usrsctplib/netinet/sctp_output.h',
        'usrsctplib/netinet/sctp_pcb.c',
        'usrsctplib/netinet/sctp_pcb.h',
        'usrsctplib/netinet/sctp_peeloff.c',
        'usrsctplib/netinet/sctp_peeloff.h',
        'usrsctplib/netinet/sctp_ss_functions.c',
        'usrsctplib/netinet/sctp_structs.h',
        'usrsctplib/netinet/sctp_sysctl.c',
        'usrsctplib/netinet/sctp_sysctl.h',
        'usrsctplib/netinet/sctp_timer.c',
        'usrsctplib/netinet/sctp_timer.h',
        'usrsctplib/netinet/sctp_uio.h',
        'usrsctplib/netinet/sctp_userspace.c',
        'usrsctplib/netinet/sctp_usrreq.c',
        'usrsctplib/netinet/sctputil.c',
        'usrsctplib/netinet/sctputil.h',
        'usrsctplib/netinet/sctp_var.h',
        'usrsctplib/netinet6/sctp6_usrreq.c',
        'usrsctplib/netinet6/sctp6_var.h',
      ],  # sources
      'conditions': [
        ['use_openssl==1', {
          'sources!': [
            'overrides/usrsctplib/netinet/sctp_nss_sha1.c',
            'overrides/usrsctplib/netinet/sctp_nss_sha1.h',
          ],
          'sources': [
            'overrides/usrsctplib/netinet/sctp_openssl_sha1.h',
          ],
        }],
        ['OS=="linux"', {
          'defines': [
            'HAVE_INET_ADDR',
            'HAVE_SOCKET',
            '__Userspace_os_Linux',
          ],
          'cflags!': [ '-Werror', '-Wall' ],
          'cflags': [ '-w' ],
        }],
        ['OS=="mac"', {
          'defines': [
            'HAVE_INET_ADDR',
            'HAVE_SA_LEN',
            'HAVE_SCONN_LEN',
            'HAVE_SIN6_LEN',
            'HAVE_SIN_LEN',
            'HAVE_SOCKET',
            'INET6',
            '__APPLE_USE_RFC_2292',
            '__Userspace_os_Darwin',
          ],
          # TODO(ldixon): explore why gyp cflags here does not get picked up.
          'xcode_settings': {
            'OTHER_CFLAGS!': [ '-Werror', '-Wall' ],
            'OTHER_CFLAGS': [ '-w' ],
          },
        }],
        ['OS=="win"', {
          'defines': [
            'INET6',
            '__Userspace_os_Windows',
          ],
          'cflags!': [ '/W3', '/WX' ],
          'cflags': [ '/w' ],
          # TODO(ldixon) : Remove this disable.
          'msvs_disabled_warnings': [ 4700, 4013, 4018, 4133, 4267 ],
        }, {  # OS != "win",
          'defines': [
            'NON_WINDOWS_DEFINE',
          ],
        }],
      ],  # conditions
    },  # target usrsctp
  ],  # targets
}
