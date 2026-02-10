#include "batch_builder.h"

namespace obd {

BatchBuilder::BatchBuilder(const std::vector<std::string>& column_names) {
    arrow::FieldVector fields;
    fields.push_back(arrow::field("timestamp_ms", arrow::int64()));
    for (const auto& name : column_names) {
        fields.push_back(arrow::field(name, arrow::float64()));
    }
    schema_ = arrow::schema(fields);

    ts_builder_ = std::make_shared<arrow::Int64Builder>();
    for (size_t i = 0; i < column_names.size(); ++i) {
        value_builders_.push_back(std::make_shared<arrow::DoubleBuilder>());
    }
}

arrow::Status BatchBuilder::append(int64_t timestamp_ms,
                                    const std::vector<std::optional<double>>& values) {
    if (values.size() != value_builders_.size()) {
        return arrow::Status::Invalid("Value count mismatch");
    }

    ARROW_RETURN_NOT_OK(ts_builder_->Append(timestamp_ms));
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i]) {
            ARROW_RETURN_NOT_OK(value_builders_[i]->Append(*values[i]));
        } else {
            ARROW_RETURN_NOT_OK(value_builders_[i]->AppendNull());
        }
    }
    ++row_count_;
    return arrow::Status::OK();
}

arrow::Result<std::shared_ptr<arrow::RecordBatch>> BatchBuilder::flush() {
    ARROW_ASSIGN_OR_RAISE(auto ts_array, ts_builder_->Finish());

    arrow::ArrayVector arrays;
    arrays.push_back(std::move(ts_array));
    for (auto& builder : value_builders_) {
        ARROW_ASSIGN_OR_RAISE(auto arr, builder->Finish());
        arrays.push_back(std::move(arr));
    }

    auto batch = arrow::RecordBatch::Make(schema_, row_count_, std::move(arrays));
    row_count_ = 0;
    return batch;
}

}  // namespace obd
