/*
    Image modifier implementation: perspective correction functions
    Copyright (C) 2015 by Torsten Bronger <bronger@physik.rwth-aachen.de>
*/

#include "config.h"
#include "lensfun.h"
#include "lensfunprv.h"
#include <cmath>
#include <vector>
#include <numeric>
#include <iostream>
#include "windows/mathconstants.h"

using std::acos;
using std::atan;
using std::atan2;
using std::cos;
using std::fabs;
using std::fmod;
using std::log;
using std::pow;
using std::sin;
using std::sqrt;

fvector normalize (float x, float y)
{
    float norm = sqrt (pow (x, 2) + pow (y, 2));
    float temp[] = {x / norm, y / norm};
    return fvector (temp, temp + 2);
}

void central_projection (fvector coordinates, float plane_distance, float &x, float &y)
{
    float stretch_factor = plane_distance / coordinates [2];
    x = coordinates [0] * stretch_factor;
    y = coordinates [1] * stretch_factor;
}

fvector svd (matrix M)
{
    const int n = M [0].size();
    fvector S2 (n);
    int  i, j, k, estimated_column_rank = n, counter = n, iterations = 0,
        max_cycles = (n < 120) ? 30 : n / 4;
    float epsilon = std::numeric_limits<float>::epsilon() * 10,
        e2 = 10 * n * pow (epsilon, 2),
        threshold = 0.1 * epsilon,
        vt, p, x0, y0, q, r, c0, s0, d1, d2;

    M.resize (2 * n, fvector (n));
    for (i = 0; i < n; i++)
        M [n + i][i] = 1;

    while (counter != 0 && iterations++ <= max_cycles)
    {
        counter = estimated_column_rank * (estimated_column_rank - 1) / 2;
        for (j = 0; j < estimated_column_rank - 1; j++)
            for (k = j + 1; k < estimated_column_rank; k++)
            {
                p = q = r = 0;
                for (i = 0; i < n; i++)
                {
                    x0 = M [i][j];
                    y0 = M [i][k];
                    p += x0 * y0;
                    q += pow (x0, 2);
                    r += pow (y0, 2);
                }
                S2 [j] = q;
                S2 [k] = r;
                if (q >= r) {
                    if (q <= e2 * S2 [0] || fabs (p) <= threshold * q)
                        counter--;
                    else
                    {
                        p /= q;
                        r = 1 - r / q;
                        vt = sqrt (4 * pow (p, 2) + pow (r, 2));
                        c0 = sqrt (0.5 * (1 + r / vt));
                        s0 = p / (vt * c0);
                        for (i = 0; i < 2 * n; i++)
                        {
                            d1 = M [i][j];
                            d2 = M [i][k];
                            M [i][j] = d1 * c0 + d2 * s0;
                            M [i][k] = - d1 * s0 + d2 * c0;
                        }
                    }
                }
                else
                {
                    p /= r;
                    q = q / r - 1;
                    vt = sqrt (4 * pow (p, 2) + pow (q, 2));
                    s0 = sqrt (0.5 * (1 - q / vt));
                    if (p < 0)
                        s0 = - s0;
                    c0 = p / (vt * s0);
                    for (i = 0; i < 2 * n; i++)
                    {
                        d1 = M [i][j];
                        d2 = M [i][k];
                        M [i][j] = d1 * c0 + d2 * s0;
                        M [i][k] = - d1 * s0 + d2 * c0;
                    }
                }
            }
        while (estimated_column_rank > 2 &&
               S2 [estimated_column_rank - 1] <=
               S2 [0] * threshold + pow (threshold, 2))
            estimated_column_rank--;
    }
    if (iterations > max_cycles)
        g_warning ("[Lensfun] SVD: Iterations did non converge");

    fvector result;
    for (matrix::iterator it = M.begin() + n; it != M.end(); ++it)
        result.push_back ((*it) [n - 1]);
    return result;
}

void ellipse_analysis (fvector x, fvector y, float f_normalized, float &x_v, float &y_v,
                       float &center_x, float &center_y)
{
    matrix M;
    float a, b, c, d, f, g, _D, x0, y0, phi, _N, _S, _R, a_, b_, radius_vertex;

    // Taken from http://math.stackexchange.com/a/767126/248694
    for (int i = 0; i < 5; i++)
    {
        float temp[] = {pow (x [i], 2), x [i] * y [i], pow (y [i], 2), x [i], y [i], 1};
        M.push_back (fvector (temp, temp + 6));
    }
    fvector parameters = svd (M);
    /* Taken from http://mathworld.wolfram.com/Ellipse.html, equation (15)
       onwards. */
    a = parameters [0];
    b = parameters [1] / 2;
    c = parameters [2];
    d = parameters [3] / 2;
    f = parameters [4] / 2;
    g = parameters [5];

    _D = pow (b, 2) - a * c;
    x0 = (c * d - b * f) / _D;
    y0 = (a * f - b * d) / _D;

    phi = 1/2 * atan (2 * b / (a - c));
    if (a > c)
        phi += M_PI_2;

    _N = 2 * (a * pow (f, 2) + c * pow (d, 2) + g * pow (b, 2) - 2 * b * d * f - a * c * g) / _D;
    _S = sqrt (pow ((a - c), 2) + 4 * pow (b, 2));
    _R = a + c;
    a_ = sqrt (_N / (_S - _R));
    b_ = sqrt (_N / (- _S - _R));
    // End taken from mathworld
    if (a_ < b_)
    {
        float temp;
        temp = a_;
        a_ = b_;
        b_ = temp;
        phi -= M_PI_2;
    }
    /* Normalize to -π/2..π/2 so that the vertex half-plane is top or bottom
       rather than e.g. left or right. */
    phi = fmod (phi + M_PI_2, M_PI) - M_PI_2;

    /* Negative sign because vertex at top (negative y values) should be
       default. */
    radius_vertex = - f_normalized / sqrt (pow (a_ / b_, 2) - 1);
    if ((x [0] - x0) * (y [1] - y0) < (x [1] - x0) * (y [0] - y0))
        radius_vertex *= -1;

    x_v = radius_vertex * sin (phi);
    y_v = radius_vertex * cos (phi);
    center_x = x0;
    center_y = y0;
}

/*
  In the following, I refer to these two rotation matrices: (See
  <http://en.wikipedia.org/wiki/Rotation_matrix#In_three_dimensions>.)

          ⎛ 1     0         0   ⎞
  Rₓ(ϑ) = ⎜ 0   cos ϑ   - sin ϑ ⎟
          ⎝ 0   sin ϑ     cos ϑ ⎠

           ⎛  cos ϑ   0   sin ϑ ⎞
  R_y(ϑ) = ⎜   0      1    0    ⎟
           ⎝- sin ϑ   0   cos ϑ ⎠

           ⎛ cos ϑ   - sin ϑ  0 ⎞
  R_z(ϑ) = ⎜ sin ϑ     cos ϑ  0 ⎟
           ⎝   0         0    1 ⎠
*/

void intersection (fvector x, fvector y, float &x_i, float &y_i)
{
    float A, B, C, numerator_x, numerator_y;

    A = x [0] * y [1] - y [0] * x [1];
    B = x [2] * y [3] - y [2] * x [3];
    C = (x [0] - x [1]) * (y [2] - y [3]) - (y [0] - y [1]) * (x [2] - x [3]);

    numerator_x = (A * (x [2] - x [3]) - B * (x [0] - x [1]));
    numerator_y = (A * (y [2] - y [3]) - B * (y [0] - y [1]));

    x_i = numerator_x / C;
    y_i = numerator_y / C;
}

fvector rotate_rho_delta (float rho, float delta, float x, float y, float z)
{
    // This matrix is: Rₓ(δ) · R_y(ρ)
    float A11, A12, A13, A21, A22, A23, A31, A32, A33;
    A11 = cos (rho);
    A12 = 0;
    A13 = sin (rho);
    A21 = sin (rho) * sin (delta);
    A22 = cos (delta);
    A23 = - cos (rho) * sin (delta);
    A31 = - sin (rho) * cos (delta);
    A32 = sin (delta);
    A33 = cos (rho) * cos (delta);

    fvector result (3);
    result [0] = A11 * x + A12 * y + A13 * z;
    result [1] = A21 * x + A22 * y + A23 * z;
    result [2] = A31 * x + A32 * y + A33 * z;
    return result;
}

fvector rotate_rho_delta_rho_h (float rho, float delta, float rho_h,
                                float x, float y, float z)
{
    // This matrix is: R_y(ρₕ) · Rₓ(δ) · R_y(ρ)
    float A11, A12, A13, A21, A22, A23, A31, A32, A33;
    A11 = cos (rho) * cos (rho_h) - sin (rho) * cos (delta) * sin (rho_h);
    A12 = sin (delta) * sin (rho_h);
    A13 = sin (rho) * cos (rho_h) + cos (rho) * cos (delta) * sin (rho_h);
    A21 = sin (rho) * sin (delta);
    A22 = cos (delta);
    A23 = - cos (rho) * sin (delta);
    A31 = - cos (rho) * sin (rho_h) - sin (rho) * cos (delta) * cos (rho_h);
    A32 = sin (delta) * cos (rho_h);
    A33 = - sin (rho) * sin (rho_h) + cos (rho) * cos (delta) * cos (rho_h);

    fvector result (3);
    result [0] = A11 * x + A12 * y + A13 * z;
    result [1] = A21 * x + A22 * y + A23 * z;
    result [2] = A31 * x + A32 * y + A33 * z;
    return result;
}

float determine_rho_h (float rho, float delta, fvector x, fvector y,
                       float f_normalized, float center_x, float center_y)
{
    fvector p0, p1;
    p0 = rotate_rho_delta (rho, delta, x [0], y [0], f_normalized);
    p1 = rotate_rho_delta (rho, delta, x [0], y [0], f_normalized);
    float x_0 = p0 [0], y_0 = p0 [1], z_0 = p0 [2];
    float x_1 = p1 [0], y_1 = p1 [1], z_1 = p1 [2];
    if (y_0 == y_1)
        return y_0 == 0 ? NAN : 0;
    else
    {
        float Delta_x, Delta_z, x_h, z_h, rho_h;
        float temp[] = {x_1 - x_0, z_1 - z_0, y_1 - y_0};
        central_projection (fvector (temp, temp + 3), - y_0, Delta_x, Delta_z);
        x_h = x_0 + Delta_x;
        z_h = z_0 + Delta_z;
        if (z_h == 0)
            rho_h = x_h > 0 ? 0 : M_PI;
        else
            rho_h = M_PI_2 - atan (x_h / z_h);
        if (rotate_rho_delta_rho_h (rho, delta, rho_h, center_x, center_y, f_normalized) [2] < 0)
            rho_h -= M_PI;
        return rho_h;
    }
}

void calculate_angles (fvector x, fvector y, float f_normalized,
                       float &rho, float &delta, float &rho_h, float &alpha,
                       float &center_of_control_points_x, float &center_of_control_points_y)
{
    const int number_of_control_points = x.size();

    float center_x, center_y;
    if (number_of_control_points == 6)
    {
        center_x = std::accumulate (x.begin(), x.begin() + 4, 0.) / 4;
        center_y = std::accumulate (y.begin(), y.begin() + 4, 0.) / 4;
    }
    else
    {
        center_x = std::accumulate (x.begin(), x.end(), 0.) / number_of_control_points;
        center_y = std::accumulate (y.begin(), y.end(), 0.) / number_of_control_points;
    }

    float x_v, y_v;
    if (number_of_control_points == 5 || number_of_control_points == 7)
        ellipse_analysis (fvector (x.begin(), x.begin() + 5), fvector (y.begin(), y.begin() + 5),
                          f_normalized, x_v, y_v, center_x, center_y);
    else
    {
        intersection (fvector (x.begin(), x.begin() + 4),
                      fvector (y.begin(), y.begin() + 4),
                      x_v, y_v);
        if (number_of_control_points == 8)
        {
            /* The problem is over-determined.  I prefer the fourth line over
               the focal length.  Maybe this is useful in cases where the focal
               length is not known. */
            float x_h, y_h;
            intersection (fvector (x.begin() + 4, x.begin() + 8),
                          fvector (y.begin() + 4, y.begin() + 8),
                          x_h, y_h);
            float radicand = - x_h * x_v - y_h * y_v;
            if (radicand >= 0)
                f_normalized = sqrt (radicand);
        }
    }

    rho = atan (- x_v / f_normalized);
    delta = M_PI_2 - atan (- y_v / sqrt (pow (x_v, 2) + pow (f_normalized, 2)));
    if (rotate_rho_delta (rho, delta, center_x, center_y, f_normalized) [2] < 0)
        // We have to move the vertex into the nadir instead of the zenith.
        delta -= M_PI;

    bool swapped_verticals_and_horizontals = false;

    fvector c (2);
    switch (number_of_control_points) {
    case 4:
    case 6:
    case 8:
    {
        fvector a = normalize (x_v - x [0], y_v - y [0]);
        fvector b = normalize (x_v - x [2], y_v - y [2]);
        c [0] = a [0] + b [0];
        c [1] = a [1] + b [1];
        break;
    }
    case 5:
    {
        c [0] = x_v - center_x;
        c [1] = y_v - center_y;
        break;
    }
    default:
    {
        c [0] = x [5] - x [6];
        c [1] = y [5] - y [6];
    }
    }
    if (number_of_control_points == 7)
    {
        float x5_, y5_;
        central_projection (rotate_rho_delta (rho, delta, x [5], y [5], f_normalized), f_normalized, x5_, y5_);
        float x6_, y6_;
        central_projection (rotate_rho_delta (rho, delta, x [6], y [6], f_normalized), f_normalized, x6_, y6_);
        alpha = - atan2 (y6_ - y5_, x6_ - x5_);
        if (fabs (c [0]) > fabs (c [1]))
            // Find smallest rotation into horizontal
            alpha = - fmod (alpha - M_PI_2, M_PI) - M_PI_2;
        else
            // Find smallest rotation into vertical
            alpha = - fmod (alpha, M_PI) - M_PI_2;
    }
    else if (fabs (c [0]) > fabs (c [1]))
    {
        swapped_verticals_and_horizontals = true;
        alpha = rho > 0 ? M_PI_2 : - M_PI_2;
    }
    else
        alpha = 0;

    /* Calculate angle of intersection of horizontal great circle with equator,
       after the vertex was moved into the zenith */
    if (number_of_control_points == 4)
    {
        fvector x_perpendicular_line (2), y_perpendicular_line (2);
        if (swapped_verticals_and_horizontals)
        {
            x_perpendicular_line [0] = center_x;
            x_perpendicular_line [1] = center_x;
            y_perpendicular_line [0] = center_y - 1;
            y_perpendicular_line [1] = center_y + 1;
        }
        else
        {
            x_perpendicular_line [0] = center_x - 1;
            x_perpendicular_line [1] = center_x + 1;
            y_perpendicular_line [0] = center_y;
            y_perpendicular_line [1] = center_y;
        }
        rho_h = determine_rho_h (rho, delta, x_perpendicular_line, y_perpendicular_line, f_normalized, center_x, center_y);
        if (isnan (rho_h))
            rho_h = 0;
    }
    else if (number_of_control_points == 5 || number_of_control_points == 7)
        rho_h = 0;
    else
    {
        rho_h = determine_rho_h (rho, delta, fvector (x.begin() + 4, x.begin() + 6),
                                 fvector (y.begin() + 4, y.begin() + 6), f_normalized, center_x, center_y);
        if (isnan (rho_h))
            if (number_of_control_points == 8)
                rho_h = determine_rho_h (rho, delta, fvector (x.begin() + 6, x.begin() + 8),
                                         fvector (y.begin() + 6, y.begin() + 8), f_normalized, center_x, center_y);
            else
                rho_h = 0;
    }
}

matrix generate_rotation_matrix (float rho_1, float delta, float rho_2, float d)
{
    float s_rho_2, c_rho_2, s_delta, c_delta, s_rho_1, c_rho_1,
        w, x, y, z, theta, s_theta;
    /* We calculate the quaternion by multiplying the three quaternions for the
       three rotations (in reverse order).  We use quaternions here to be able
       to apply the d parameter in a reasonable way. */
    s_rho_2 = sin (rho_2 / 2);
    c_rho_2 = cos (rho_2 / 2);
    s_delta = sin (delta / 2);
    c_delta = cos (delta / 2);
    s_rho_1 = sin (rho_1 / 2);
    c_rho_1 = cos (rho_1 / 2);
    w = c_rho_2 * c_delta * c_rho_1 - s_rho_2 * c_delta * s_rho_1;
    x = c_rho_2 * s_delta * c_rho_1 + s_rho_2 * s_delta * s_rho_1;
    y = c_rho_2 * c_delta * s_rho_1 + s_rho_2 * c_delta * c_rho_1;
    z = c_rho_2 * s_delta * s_rho_1 - s_rho_2 * s_delta * c_rho_1;
    // Now, decompose the quaternion into θ and the axis unit vector.
    theta = 2 * acos (w);
    if (theta > M_PI)
        theta -= 2 * M_PI;
    s_theta = sin (theta / 2);
    x /= s_theta;
    y /= s_theta;
    z /= s_theta;
    const float compression = 10;
    theta *= d <= 0 ? d + 1 : 1 + 1. / compression * log (compression * d + 1);
    if (theta > 0.9 * M_PI)
        theta = 0.9 * M_PI;
    else if (theta < - 0.9 * M_PI)
        theta = - 0.9 * M_PI;
    // Compose the quaternion again.
    w = cos (theta / 2);
    s_theta = sin (theta / 2);
    x *= s_theta;
    y *= s_theta;
    z *= s_theta;
    /* Convert the quaternion to a rotation matrix, see e.g.
       <https://en.wikipedia.org/wiki/Rotation_matrix#Quaternion>.  This matrix
       is (if d=0): R_y(ρ2) · Rₓ(δ) · R_y(ρ1) */
    matrix M (3, fvector (3));
    M [0][0] = 1 - 2 * pow (y, 2) - 2 * pow (z, 2);
    M [0][1] = 2 * x * y - 2 * z * w;
    M [0][2] = 2 * x * z + 2 * y * w;
    M [1][0] = 2 * x * y + 2 * z * w;
    M [1][1] = 1 - 2 * pow (x, 2) - 2 * pow (z, 2);
    M [1][2] = 2 * y * z - 2 * x * w;
    M [2][0] = 2 * x * z - 2 * y * w;
    M [2][1] = 2 * y * z + 2 * x * w;
    M [2][2] = 1 - 2 * pow (x, 2) - 2 * pow (y, 2);
    return M;
}

bool lfModifier::enable_perspective_correction (fvector x, fvector y, float d)
{
    const int number_of_control_points = x.size();
    if (f_normalized <= 0 || number_of_control_points < 4 || number_of_control_points > 8)
        return false;
    if (d < -1)
        d = -1;
    if (d > 1)
        d = 1;
    for (int i = 0; i < number_of_control_points; i++)
    {
        x [i] = x [i] * NormScale - CenterX;
        y [i] = y [i] * NormScale - CenterY;
    }

    float rho, delta, rho_h, alpha, center_of_control_points_x,
        center_of_control_points_y, z;
    calculate_angles (x, y, f_normalized, rho, delta, rho_h, alpha,
                      center_of_control_points_x, center_of_control_points_y);

    // Transform center point to get shift
    z = rotate_rho_delta_rho_h (rho, delta, rho_h, 0, 0, f_normalized) [2];
    /* If the image centre is too much outside, or even at infinity, take the
       center of gravity of the control points instead. */
    enum center_type { old_image_center, control_points_center };
    center_type new_image_center = z <= 0 || f_normalized / z > 10 ? control_points_center : old_image_center;

    /* Generate a rotation matrix in forward direction, for getting the
       proper shift of the image center. */
    matrix A = generate_rotation_matrix (rho, delta, rho_h, d);
    fvector center_coords (3);

    switch (new_image_center) {
    case old_image_center:
    {
        center_coords [0] = A [0][2] * f_normalized;
        center_coords [1] = A [1][2] * f_normalized;
        center_coords [2] = A [2][2] * f_normalized;
        break;
    }
    case control_points_center:
    {
        center_coords [0] = A [0][0] * center_of_control_points_x +
                            A [0][1] * center_of_control_points_y +
                            A [0][2] * f_normalized;
        center_coords [1] = A [1][0] * center_of_control_points_x +
                            A [1][1] * center_of_control_points_y +
                            A [1][2] * f_normalized;
        center_coords [2] = A [2][0] * center_of_control_points_x +
                            A [2][1] * center_of_control_points_y +
                            A [2][2] * f_normalized;
        break;
    }
    }
    if (center_coords [2] <= 0)
        return false;
    // This is the mapping scale in the image center
    float mapping_scale = f_normalized / center_coords [2];

    // Finally, generate a rotation matrix in backward (lookup) direction
    A = generate_rotation_matrix (- rho_h, - delta, - rho, d);

    /* Now we append the final rotation by α.  This matrix is: R_y(- ρ) ·
       Rₓ(- δ) · R_y(- ρₕ) · R_z(α). */
    A [0][0] = cos (alpha) * A [0][0] + sin (alpha) * A [0][1];
    A [0][1] = - sin (alpha) * A [0][0] + cos (alpha) * A [0][1];
    A [0][2] = A [0][2];
    A [1][0] = cos (alpha) * A [1][0] + sin (alpha) * A [1][1];
    A [1][1] = - sin (alpha) * A [1][0] + cos (alpha) * A [1][1];
    A [1][2] = A [1][2];
    A [2][0] = cos (alpha) * A [2][0] + sin (alpha) * A [2][1];
    A [2][1] = - sin (alpha) * A [2][0] + cos (alpha) * A [2][1];
    A [2][2] = A [2][2];
    float Delta_a, Delta_b;
    central_projection (center_coords, f_normalized, Delta_a, Delta_b);
    Delta_a = cos (alpha) * Delta_a + sin (alpha) * Delta_b;
    Delta_b = - sin (alpha) * Delta_a + cos (alpha) * Delta_b;

    /* The occurances of mapping_scale here avoid an additional multiplication
       in the inner loop of perspective_correction_callback. */
    float tmp[] = {A [0][0] * mapping_scale, A [0][1] * mapping_scale, A [0][2],
                   A [1][0] * mapping_scale, A [1][1] * mapping_scale, A [1][2],
                   A [2][0] * mapping_scale, A [2][1] * mapping_scale, A [2][2],
                   f_normalized, Delta_a / mapping_scale, Delta_b / mapping_scale};
    AddCoordCallback (ModifyCoord_Perspective_Correction, 200, tmp, sizeof (tmp));
    return true;
}

void lfModifier::ModifyCoord_Perspective_Correction (void *data, float *iocoord, int count)
{
    // Rd = Ru * (1 - k1 + k1 * Ru^2)
    const float k1 = *(float *)data;
    const float one_minus_k1 = 1.0 - k1;

    for (float *end = iocoord + count * 2; iocoord < end; iocoord += 2)
    {
        const float x = iocoord [0];
        const float y = iocoord [1];
        const float poly2 = one_minus_k1 + k1 * (pow (x, 2) + pow (y, 2));

        iocoord [0] = x * poly2;
        iocoord [1] = y * poly2;
    }
}