#pragma once

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace obd {

class BatchBuilder {
public:
    // Column names (excluding timestamp which is always first)
    explicit BatchBuilder(const std::vector<std::string>& column_names);

    // Append a row: timestamp_ms + one value per column (nullopt for missing)
    arrow::Status append(int64_t timestamp_ms, const std::vector<std::optional<double>>& values);

    // Flush accumulated rows into a RecordBatch and reset builders
    arrow::Result<std::shared_ptr<arrow::RecordBatch>> flush();

    int64_t row_count() const { return row_count_; }

    std::shared_ptr<arrow::Schema> schema() const { return schema_; }

private:
    std::shared_ptr<arrow::Schema> schema_;
    std::shared_ptr<arrow::Int64Builder> ts_builder_;
    std::vector<std::shared_ptr<arrow::DoubleBuilder>> value_builders_;
    int64_t row_count_ = 0;
};

}  // namespace obd
