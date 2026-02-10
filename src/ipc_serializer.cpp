#include "ipc_serializer.h"

namespace obd {

arrow::Result<std::shared_ptr<arrow::Buffer>> serialize_batch(
    const std::shared_ptr<arrow::RecordBatch>& batch) {
    ARROW_ASSIGN_OR_RAISE(auto sink, arrow::io::BufferOutputStream::Create());
    ARROW_ASSIGN_OR_RAISE(auto writer, arrow::ipc::MakeStreamWriter(sink, batch->schema()));
    ARROW_RETURN_NOT_OK(writer->WriteRecordBatch(*batch));
    ARROW_RETURN_NOT_OK(writer->Close());
    return sink->Finish();
}

}  // namespace obd
