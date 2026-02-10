#pragma once

#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/writer.h>

#include <memory>

namespace obd {

// Serialize a RecordBatch to Arrow IPC stream format (suitable for Perspective)
arrow::Result<std::shared_ptr<arrow::Buffer>> serialize_batch(
    const std::shared_ptr<arrow::RecordBatch>& batch);

}  // namespace obd
