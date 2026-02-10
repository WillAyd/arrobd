#include "../src/batch_builder.h"
#include "../src/ipc_serializer.h"

#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/reader.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

void test_build_and_flush() {
    obd::BatchBuilder builder({"rpm", "speed_kmh", "coolant_temp_c"});

    auto s1 = builder.append(1000, {1726.0, 60.0, 83.0});
    assert(s1.ok());
    auto s2 = builder.append(2000, {1800.0, 65.0, std::nullopt});
    assert(s2.ok());

    assert(builder.row_count() == 2);

    auto batch_result = builder.flush();
    assert(batch_result.ok());
    auto batch = *batch_result;

    assert(batch->num_rows() == 2);
    assert(batch->num_columns() == 4);  // timestamp + 3 values
    assert(batch->schema()->field(0)->name() == "timestamp_ms");
    assert(batch->schema()->field(1)->name() == "rpm");
    assert(batch->schema()->field(2)->name() == "speed_kmh");
    assert(batch->schema()->field(3)->name() == "coolant_temp_c");

    // Check values
    auto ts_arr = std::static_pointer_cast<arrow::Int64Array>(batch->column(0));
    assert(ts_arr->Value(0) == 1000);
    assert(ts_arr->Value(1) == 2000);

    auto rpm_arr = std::static_pointer_cast<arrow::DoubleArray>(batch->column(1));
    assert(std::abs(rpm_arr->Value(0) - 1726.0) < 0.01);
    assert(std::abs(rpm_arr->Value(1) - 1800.0) < 0.01);

    // Check null
    auto coolant_arr = std::static_pointer_cast<arrow::DoubleArray>(batch->column(3));
    assert(!coolant_arr->IsNull(0));
    assert(coolant_arr->IsNull(1));

    assert(builder.row_count() == 0);  // reset after flush
    std::cerr << "  build_and_flush: OK\n";
}

void test_ipc_roundtrip() {
    obd::BatchBuilder builder({"rpm", "speed_kmh"});
    auto s = builder.append(1000, {1500.0, 80.0});
    assert(s.ok());

    auto batch_result = builder.flush();
    assert(batch_result.ok());

    // Serialize
    auto buf_result = obd::serialize_batch(*batch_result);
    assert(buf_result.ok());
    auto buffer = *buf_result;
    assert(buffer->size() > 0);

    // Deserialize and verify
    auto input = std::make_shared<arrow::io::BufferReader>(buffer);
    auto reader_result = arrow::ipc::RecordBatchStreamReader::Open(input);
    assert(reader_result.ok());
    auto reader = *reader_result;

    std::shared_ptr<arrow::RecordBatch> read_batch;
    auto read_status = reader->ReadNext(&read_batch);
    assert(read_status.ok());
    assert(read_batch != nullptr);
    assert(read_batch->num_rows() == 1);
    assert(read_batch->num_columns() == 3);

    auto rpm_arr = std::static_pointer_cast<arrow::DoubleArray>(read_batch->column(1));
    assert(std::abs(rpm_arr->Value(0) - 1500.0) < 0.01);

    std::cerr << "  ipc_roundtrip: OK\n";
}

int main() {
    std::cerr << "Running Arrow batch tests...\n";
    test_build_and_flush();
    test_ipc_roundtrip();
    std::cerr << "All Arrow batch tests passed.\n";
    return 0;
}
