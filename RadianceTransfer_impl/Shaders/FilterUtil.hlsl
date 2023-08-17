double calcPixelCoordTerm(float ix, float iy, float jx, float jy, double sigma)
{
    return (pow(ix - jx, 2) + pow(iy - jy, 2)) / (2 * pow(sigma, 2));
}

double calcNormalTerm(float3 n1, float3 n2, double sigma)
{
    return pow(acos(dot(n1, n2)), 2) / (2 * pow(sigma, 2));
}

double calcPlaneTerm(float3 normalI, float3 posI, float3 posJ, double sigma)
{
    if (!length(posI - posJ))
        return 0.0;

    return pow(dot(normalI, normalize(posJ - posI)), 2) / (2 * pow(sigma, 2));
}

double calcColorTerm(float3 colorI, float3 colorJ, double sigma)
{
    return pow(distance(colorI, colorJ), 2) / (2 * pow(sigma, 2));
}

static const double sigmaCoord = 32.0;
static const double sigmaNormal = 0.2;
static const double sigmaPlane = 0.5;
static const double sigmaColor = 0.6;
static const double sigmaClamp = 1.0f;
static const double sigmaOutlierRemoval = 1.0f;
static const int radius = 32;