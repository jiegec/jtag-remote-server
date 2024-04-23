project('jtag-remote-server', 'cpp')

libftdi = dependency('libftdi1')

executable('jtag-remote-server', 'src/main.cpp', 'src/xvc.cpp',
           'src/rbb.cpp', 'src/common.cpp', 'src/vpi.cpp',
           'src/jtagd.cpp', 'src/mpsse.cpp', 'src/mpsse_buffer.cpp',
           'src/usb_blaster.cpp',
           dependencies : libftdi,
           override_options : ['cpp_std=c++11'],
           install : true)

install_data('README.md',
             install_dir : get_option('datadir') / 'jtag-remote-server')
install_subdir('example',
               install_dir : get_option('datadir') / 'jtag-remote-server')
