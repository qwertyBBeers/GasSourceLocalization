#pragma once
#include <vector>
#include <gsl_server/core/Vectors.hpp>

namespace GSL
{

    struct GridData
    {
        Vector2 origin;
        float cellSize;
        int numFreeCells;
        Vector2Int coordinatesToIndex(double x, double y) const
        {
            return Vector2Int((y - origin.y) / (cellSize), (x - origin.x) / (cellSize));
        }

        Vector2 indexToCoordinates(int i, int j, bool centerOfCell = true) const
        {
            float offset = centerOfCell ? 0.5 : 0;
            return Vector2(origin.x + (j + offset) * cellSize, origin.y + (i + offset) * cellSize);
        }
    };
} // namespace GSL