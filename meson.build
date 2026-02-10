project('arrowbd2', 'cpp', 'c',
  version : '0.1.0',
  default_options : ['cpp_std=c++23', 'warning_level=2'])

arrow_dep = dependency('arrow')
lws_dep = dependency('libwebsockets')
thread_dep = dependency('threads')

mock_mode = get_option('mock_mode')

src = files(
  'src/main.cpp',
  'src/serial.cpp',
  'src/elm327.cpp',
  'src/pids.cpp',
  'src/batch_builder.cpp',
  'src/ipc_serializer.cpp',
  'src/ws_server.cpp',
  'src/pipeline.cpp',
)
if mock_mode
  src += files('src/mock_serial.cpp')
endif

executable('arrowbd2', src,
  dependencies : [arrow_dep, lws_dep, thread_dep],
  cpp_args : mock_mode ? ['-DARROWBD2_MOCK=1'] : [],
)

subdir('test')
