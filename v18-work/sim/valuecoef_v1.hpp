// valuecoef.hpp - coefficients of the linear state-value model (tools/fit_value.py, ridge regression of the final
// score margin on state features, samples from self-play rollouts of the v17-family policies on dev seeds)
#pragma once
namespace vc {
constexpr double VB = 0.658396;
constexpr double VW[28] = {0.332800, -0.224869, 0.058845, 0.415000, 1.145090, 0.174790, 4.074066, 3.723533, 1.863583, 2.470085, 0.371285, 0.418790, -0.371434, 0.483667, 0.295547, -0.502308, -0.535769, 0.000000, 0.000000, -0.534394, -0.712210, 0.465571, 0.003720, -0.026774, 0, 0, 0, 0};
}
