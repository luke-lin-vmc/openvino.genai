// Copyright (C) 2024 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "statefull_decoder.hpp"

#include "debug_utils.hpp"
#include "utils.hpp"

namespace ov::genai {
WhisperStatefullDecoder::WhisperStatefullDecoder(const std::filesystem::path& models_path,
                                                 const std::string& device,
                                                 const ov::AnyMap& properties) {
    ov::Core core = utils::singleton_core();

    auto model = core.read_model(models_path / "openvino_decoder_model.xml", {}, properties);

    utils::apply_slice_before_matmul_transformation(model);

    auto compiled_model = core.compile_model(model, device, properties);

    utils::print_compiled_model_properties(compiled_model, "whisper decoder model");
    m_request = compiled_model.create_infer_request();
}

std::pair<ov::Tensor, float> WhisperStatefullDecoder::decode(const Tensor& encoder_hidden_state,
                                                             const Tensor& input_ids,
                                                             const Tensor& beam_idx) {
    const size_t batch_size = input_ids.get_shape().at(0);
    const size_t seq_len = input_ids.get_shape().at(1);

    _set_encoder_hidden_states_tensor(encoder_hidden_state, batch_size, m_request);

    _set_cache_position_tensor(seq_len);
    m_request.set_tensor("input_ids", input_ids);
    m_request.set_tensor("beam_idx", beam_idx);

    const auto infer_start = std::chrono::steady_clock::now();
    m_request.infer();
    const auto infer_ms = ov::genai::PerfMetrics::get_microsec(std::chrono::steady_clock::now() - infer_start);

    auto output_tensor = m_request.get_tensor("logits");

    return {output_tensor, infer_ms};
};

void WhisperStatefullDecoder::_set_cache_position_tensor(const size_t seq_len) {
    ov::Tensor cache_position_tensor = m_request.get_tensor("cache_position");

    int64_t start_cache_position = 0;

    if (cache_position_tensor.get_size() != 0) {
        start_cache_position = cache_position_tensor.data<int64_t>()[cache_position_tensor.get_size() - 1] + 1;
    }

    cache_position_tensor.set_shape({seq_len});

    auto cache_data = cache_position_tensor.data<int64_t>();
    std::iota(cache_data, cache_data + seq_len, start_cache_position);
};

void WhisperStatefullDecoder::reset_state() {
    m_request.reset_state();
    m_request.set_tensor("cache_position", ov::Tensor{ov::element::i64, {0}});

    Shape encoder_hidden_states_shape{m_request.get_tensor("encoder_hidden_states").get_shape()};
    encoder_hidden_states_shape[0] = 0;
    m_request.set_tensor("encoder_hidden_states", ov::Tensor{ov::element::f32, encoder_hidden_states_shape});
};

}  // namespace ov::genai
