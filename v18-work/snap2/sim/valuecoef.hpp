// valuecoef.hpp - coefficients of the linear state-value model (tools/fit_value.py, ridge regression of the final
// score margin on state features, samples from self-play rollouts of the v17-family policies on dev seeds)
#pragma once
namespace vc {
constexpr double VB = 0.585614;
constexpr double VW[28] = {0.341879, -0.227637, 0.071411, 0.385175, 1.210045, 0.194485, 4.957109, 3.514588, 1.922421, 3.225509, 0.363803, 0.508481, -0.334790, 0.565405, 0.491211, -0.535184, -0.563195, 0.000000, 0.000000, -0.472545, -0.947906, 0.481340, 0.003277, -0.019494, 1.373525, -1.128232, 0.239474, -0.067828};
}
