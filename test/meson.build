test_elm327 = executable('test_elm327', 'test_elm327.cpp',
  '../src/elm327.cpp', '../src/pids.cpp',
  dependencies : [arrow_dep],
)
test('elm327_parsing', test_elm327)

test_batch = executable('test_batch', 'test_batch.cpp',
  '../src/batch_builder.cpp', '../src/ipc_serializer.cpp',
  dependencies : [arrow_dep],
)
test('arrow_batch', test_batch)
