#pragma once

#include <catmull_clark/common.h>

/**
 * @brief 在网格模型上应用指定次数的Catmull-Clark细分算法，返回细分后的新模型
 */
Mesh applyCatmullClarkSubdivision(const Mesh &originalMesh, int iterationCount);
