/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "paddle/fluid/inference/tests/api/tester_helper.h"

namespace paddle {
namespace inference {

DEFINE_bool(use_tensorrt, true, "Test the performance of TensorRT engine.");
DEFINE_string(prog_filename, "", "Name of model file.");
DEFINE_string(param_filename, "", "Name of parameters file.");

template <typename ConfigType>
void SetConfig(ConfigType* config, std::string model_dir, bool use_gpu,
               bool use_tensorrt = false, int batch_size = -1) {
  if (!FLAGS_prog_filename.empty() && !FLAGS_param_filename.empty()) {
    config->prog_file = model_dir + "/" + FLAGS_prog_filename;
    config->param_file = model_dir + "/" + FLAGS_param_filename;
  } else {
    config->model_dir = model_dir;
  }
  if (use_gpu) {
    config->use_gpu = true;
    config->device = 0;
    config->fraction_of_gpu_memory = 0.15;
  }
}

template <>
void SetConfig<AnalysisConfig>(AnalysisConfig* config, std::string model_dir,
                               bool use_gpu, bool use_tensorrt,
                               int batch_size) {
  if (!FLAGS_prog_filename.empty() && !FLAGS_param_filename.empty()) {
    config->SetModel(model_dir + "/" + FLAGS_prog_filename,
                     model_dir + "/" + FLAGS_param_filename);
  } else {
    config->SetModel(model_dir);
  }
  if (use_gpu) {
    config->EnableUseGpu(100, 0);
    if (use_tensorrt) {
      config->EnableTensorRtEngine(1 << 10, batch_size, 3,
                                   AnalysisConfig::Precision::kFloat32, false);
      config->pass_builder()->DeletePass("conv_bn_fuse_pass");
      config->pass_builder()->DeletePass("fc_fuse_pass");
      config->pass_builder()->TurnOnDebug();
    } else {
      config->SwitchIrOptim();
    }
  }
}

void profile(std::string model_dir, bool use_analysis, bool use_tensorrt) {
  std::vector<std::vector<PaddleTensor>> inputs_all;
  if (!FLAGS_prog_filename.empty() && !FLAGS_param_filename.empty()) {
    SetFakeImageInput(&inputs_all, model_dir, true, FLAGS_prog_filename,
                      FLAGS_param_filename);
  } else {
    SetFakeImageInput(&inputs_all, model_dir, false, "__model__", "");
  }

  std::vector<PaddleTensor> outputs;
  if (use_analysis || use_tensorrt) {
    AnalysisConfig config;
    config.EnableUseGpu(100, 0);
    config.pass_builder()->TurnOnDebug();
    SetConfig<AnalysisConfig>(&config, model_dir, true, use_tensorrt,
                              FLAGS_batch_size);
    TestPrediction(reinterpret_cast<PaddlePredictor::Config*>(&config),
                   inputs_all, &outputs, FLAGS_num_threads, true);
  } else {
    NativeConfig config;
    SetConfig<NativeConfig>(&config, model_dir, true, false);
    TestPrediction(reinterpret_cast<PaddlePredictor::Config*>(&config),
                   inputs_all, &outputs, FLAGS_num_threads, false);
  }
}

void compare(std::string model_dir, bool use_tensorrt) {
  std::vector<std::vector<PaddleTensor>> inputs_all;
  if (!FLAGS_prog_filename.empty() && !FLAGS_param_filename.empty()) {
    SetFakeImageInput(&inputs_all, model_dir, true, FLAGS_prog_filename,
                      FLAGS_param_filename);
  } else {
    SetFakeImageInput(&inputs_all, model_dir, false, "__model__", "");
  }

  AnalysisConfig analysis_config;
  SetConfig<AnalysisConfig>(&analysis_config, model_dir, true, use_tensorrt,
                            FLAGS_batch_size);
  CompareNativeAndAnalysis(
      reinterpret_cast<const PaddlePredictor::Config*>(&analysis_config),
      inputs_all);
}

void compare_continuous_input(std::string model_dir, bool use_tensorrt) {
  AnalysisConfig analysis_config;
  SetConfig<AnalysisConfig>(&analysis_config, model_dir, true, use_tensorrt,
                            FLAGS_batch_size);
  auto config =
      reinterpret_cast<const PaddlePredictor::Config*>(&analysis_config);
  auto native_pred = CreateTestPredictor(config, false);
  auto analysis_pred = CreateTestPredictor(config, true);
  for (int i = 0; i < 100; i++) {
    std::vector<std::vector<PaddleTensor>> inputs_all;
    if (!FLAGS_prog_filename.empty() && !FLAGS_param_filename.empty()) {
      SetFakeImageInput(&inputs_all, model_dir, true, FLAGS_prog_filename,
                        FLAGS_param_filename, nullptr, i);
    } else {
      SetFakeImageInput(&inputs_all, model_dir, false, "__model__", "", nullptr,
                        i);
    }
    CompareNativeAndAnalysis(native_pred.get(), analysis_pred.get(),
                             inputs_all);
  }
}

TEST(TensorRT_mobilenet, compare) {
  std::string model_dir = FLAGS_infer_model + "/mobilenet";
  compare(model_dir, /* use_tensorrt */ true);
}

TEST(TensorRT_resnet50, compare) {
  std::string model_dir = FLAGS_infer_model + "/resnet50";
  compare(model_dir, /* use_tensorrt */ true);
}

TEST(TensorRT_resnext50, compare) {
  std::string model_dir = FLAGS_infer_model + "/resnext50";
  compare(model_dir, /* use_tensorrt */ true);
}

TEST(TensorRT_resnext50, profile) {
  std::string model_dir = FLAGS_infer_model + "/resnext50";
  // Set FLAGS_record_benchmark to true to record benchmark to file.
  // FLAGS_record_benchmark=true;
  FLAGS_model_name = "resnext50";
  profile(model_dir, /* use_analysis */ true, FLAGS_use_tensorrt);
}

TEST(resnext50, compare_analysis_native) {
  std::string model_dir = FLAGS_infer_model + "/resnext50";
  compare(model_dir, false /*use tensorrt*/);
}

TEST(TensorRT_mobilenet, analysis) {
  std::string model_dir = FLAGS_infer_model + "/" + "mobilenet";
  compare(model_dir, false /* use_tensorrt */);
}

TEST(AnalysisPredictor, use_gpu) {
  std::string model_dir = FLAGS_infer_model + "/" + "mobilenet";
  AnalysisConfig config;
  config.EnableUseGpu(100, 0);
  config.SetModel(model_dir);
  config.pass_builder()->TurnOnDebug();

  std::vector<std::vector<PaddleTensor>> inputs_all;
  auto predictor = CreatePaddlePredictor(config);
  SetFakeImageInput(&inputs_all, model_dir, false, "__model__", "");

  std::vector<PaddleTensor> outputs;
  for (auto& input : inputs_all) {
    ASSERT_TRUE(predictor->Run(input, &outputs));
  }
}

TEST(TensorRT_mobilenet, profile) {
  std::string model_dir = FLAGS_infer_model + "/" + "mobilenet";
  profile(model_dir, true, false);
}

TEST(resnet50, compare_continuous_input) {
  std::string model_dir = FLAGS_infer_model + "/resnet50";
  compare_continuous_input(model_dir, true);
}

TEST(resnet50, compare_continuous_input_native) {
  std::string model_dir = FLAGS_infer_model + "/resnet50";
  compare_continuous_input(model_dir, false);
}

}  // namespace inference
}  // namespace paddle
