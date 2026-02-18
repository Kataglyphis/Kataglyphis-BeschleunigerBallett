#include "kompute/Algorithm.hpp"
#include "kompute/Manager.hpp"
#include "kompute/Memory.hpp"
#include "kompute/Tensor.hpp"
#include "kompute/operations/OpAlgoDispatch.hpp"
#include "kompute/operations/OpSyncDevice.hpp"
#include "kompute/operations/OpSyncLocal.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <shader/my_shader.hpp>

auto main() -> int
{
    kp::Manager mgr;

    std::shared_ptr<kp::TensorT<float>> const tensorInA = mgr.tensor({ 2.0, 4.0, 6.0 });
    std::shared_ptr<kp::TensorT<float>> const tensorInB = mgr.tensor({ 0.0, 1.0, 2.0 });
    std::shared_ptr<kp::TensorT<float>> const tensorOut = mgr.tensor({ 0.0, 0.0, 0.0 });

    const std::vector<std::shared_ptr<kp::Memory>> params = { tensorInA, tensorInB, tensorOut };

    const std::vector<uint32_t> shader =
      std::vector<uint32_t>(shader::MY_SHADER_COMP_SPV.begin(), shader::MY_SHADER_COMP_SPV.end());
    std::shared_ptr<kp::Algorithm> const algo = mgr.algorithm(params, shader);

    mgr.sequence()
      ->record<kp::OpSyncDevice>(params)
      ->record<kp::OpAlgoDispatch>(algo)
      ->record<kp::OpSyncLocal>(params)
      ->eval();

    // prints "Output {  0  4  12  }"
    std::cout << "Output: {  ";
    for (const float &elem : tensorOut->vector()) { std::cout << elem << "  "; }
    std::cout << "}" << '\n';

    if (tensorOut->vector() != std::vector<float>{ 0, 4, 12 }) { throw std::runtime_error("Result does not match"); }
}