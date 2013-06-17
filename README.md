Parallel JavaScript (a.k.a RiverTrail) on chromium.

## Code
1. chromium: https://github.com/01org/pa-chromium.git 

  use the “master” branch.
 
2. blink: https://github.com/01org/pa-blink.git 

  use the “master” branch.

3. .gclient file https://github.com/01org/pa-chromium/blob/master/.gclient 

## Build process

- fetch chromium code:

  `git clone git@github.com:01org/pa-chromium.git src`
  
  `git checkout –b master origin/master`


- fetch blink code:

  `cd src/third-party`
  
  `git clone git@github.com:01org/pa-blink.git WebKit`
  
  `git checkout –b master oirigin/master`

- gclient sync.

  To remove the unversioned directories automatically, run `gclient sync` with `-D -R`.
  To avoid generating Visual Studio projects which will be overwritten later, add the `-n` option.

- /build/gyp_chromium (optional: -Dcomponent=shared_library -Ddisable_nacl=1)

- Build.
