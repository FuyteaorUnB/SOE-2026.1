#ifndef VISAO_CORE_HPP
#define VISAO_CORE_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <limits>

namespace soe {

constexpr int FACE_SIZE = 120;
constexpr int GRID      = 8;

inline cv::Mat tanTriggs(const cv::Mat& src,
                         float gamma  = 0.2f,
                         float sigma0 = 1.0f,
                         float sigma1 = 2.0f,
                         float alpha  = 0.1f,
                         float tau    = 10.0f)
{
    cv::Mat I;
    src.convertTo(I, CV_32F);

    cv::pow(I, gamma, I);

    cv::Mat g0, g1;
    cv::GaussianBlur(I, g0, cv::Size(0, 0), sigma0, sigma0, cv::BORDER_REPLICATE);
    cv::GaussianBlur(I, g1, cv::Size(0, 0), sigma1, sigma1, cv::BORDER_REPLICATE);
    I = g0 - g1;

    {
        cv::Mat tmp;
        cv::pow(cv::abs(I), alpha, tmp);
        double meanv = cv::mean(tmp)[0];
        I = I / std::pow(meanv, 1.0 / alpha);
    }
    {
        cv::Mat absI = cv::abs(I);
        cv::Mat minned;
        cv::min(absI, (double)tau, minned);
        cv::Mat tmp;
        cv::pow(minned, alpha, tmp);
        double meanv = cv::mean(tmp)[0];
        I = I / std::pow(meanv, 1.0 / alpha);
    }

    for (int i = 0; i < I.rows; ++i) {
        float* p = I.ptr<float>(i);
        for (int j = 0; j < I.cols; ++j)
            p[j] = tau * std::tanh(p[j] / tau);
    }

    cv::Mat out;
    cv::normalize(I, out, 0, 255, cv::NORM_MINMAX, CV_8U);
    return out;
}

inline cv::Mat preprocessarFace(const cv::Mat& grayFullFrame, const cv::Rect& r)
{
    cv::Rect rr = r & cv::Rect(0, 0, grayFullFrame.cols, grayFullFrame.rows);
    if (rr.width < 40 || rr.height < 40) return cv::Mat();
    cv::Mat roi = grayFullFrame(rr);
    cv::Mat resized;
    cv::resize(roi, resized, cv::Size(FACE_SIZE, FACE_SIZE), 0, 0, cv::INTER_AREA);
    return tanTriggs(resized);
}

inline cv::Mat computeLBP(const cv::Mat& src)
{
    cv::Mat lbp = cv::Mat::zeros(src.rows - 2, src.cols - 2, CV_8U);
    for (int i = 1; i < src.rows - 1; ++i) {
        for (int j = 1; j < src.cols - 1; ++j) {
            uchar c = src.at<uchar>(i, j);
            unsigned char code = 0;
            code |= (src.at<uchar>(i-1, j-1) >= c) << 7;
            code |= (src.at<uchar>(i-1, j  ) >= c) << 6;
            code |= (src.at<uchar>(i-1, j+1) >= c) << 5;
            code |= (src.at<uchar>(i  , j+1) >= c) << 4;
            code |= (src.at<uchar>(i+1, j+1) >= c) << 3;
            code |= (src.at<uchar>(i+1, j  ) >= c) << 2;
            code |= (src.at<uchar>(i+1, j-1) >= c) << 1;
            code |= (src.at<uchar>(i  , j-1) >= c) << 0;
            lbp.at<uchar>(i-1, j-1) = code;
        }
    }
    return lbp;
}

inline int uniformLabel(uchar code)
{
    int transicoes = 0;
    for (int b = 0; b < 8; ++b) {
        int atual    = (code >> b) & 1;
        int proximo  = (code >> ((b + 1) % 8)) & 1;
        if (atual != proximo) transicoes++;
    }
    if (transicoes <= 2) return __builtin_popcount(code);
    return 9;
}

inline double lbpUniformVar(const cv::Mat& gray120)
{
    cv::Mat lbp = computeLBP(gray120);
    std::vector<double> labels;
    labels.reserve(lbp.total());
    for (int i = 0; i < lbp.rows; ++i)
        for (int j = 0; j < lbp.cols; ++j)
            labels.push_back((double)uniformLabel(lbp.at<uchar>(i, j)));
    cv::Scalar m, s;
    cv::meanStdDev(labels, m, s);
    return s[0] * s[0];
}

inline std::vector<float> spatialHistogram(const cv::Mat& gray120)
{
    cv::Mat lbp = computeLBP(gray120);
    int gw = lbp.cols / GRID;
    int gh = lbp.rows / GRID;
    std::vector<float> feat;
    feat.reserve(GRID * GRID * 256);

    for (int gy = 0; gy < GRID; ++gy) {
        for (int gx = 0; gx < GRID; ++gx) {
            cv::Rect cellR(gx * gw, gy * gh, gw, gh);
            cv::Mat cell = lbp(cellR);
            std::vector<float> hist(256, 0.f);
            for (int y = 0; y < cell.rows; ++y)
                for (int x = 0; x < cell.cols; ++x)
                    hist[cell.at<uchar>(y, x)] += 1.f;
            float s = 0.f; for (float v : hist) s += v;
            if (s > 0) for (float& v : hist) v /= s;
            feat.insert(feat.end(), hist.begin(), hist.end());
        }
    }
    return feat;
}

inline double chiSquare(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0;
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        double soma = a[i] + b[i];
        if (soma > 1e-12) { double dif = a[i] - b[i]; d += (dif * dif) / soma; }
    }
    return d;
}

struct LBPHModel {
    int faceSize = FACE_SIZE;
    int grid     = GRID;
    std::vector<std::vector<float>> hists;
    std::vector<int> labels;

    void add(const cv::Mat& face120, int label) {
        hists.push_back(spatialHistogram(face120));
        labels.push_back(label);
    }

    std::pair<int,double> predict(const cv::Mat& face120) const {
        if (hists.empty()) return {-1, std::numeric_limits<double>::max()};
        std::vector<float> q = spatialHistogram(face120);
        double melhor = std::numeric_limits<double>::max();
        int    lab    = -1;
        for (size_t i = 0; i < hists.size(); ++i) {
            double d = chiSquare(q, hists[i]);
            if (d < melhor) { melhor = d; lab = labels[i]; }
        }
        return {lab, melhor};
    }

    bool salvar(const std::string& caminho) const {
        std::ofstream f(caminho);
        if (!f) return false;
        size_t featLen = hists.empty() ? 0 : hists[0].size();
        f << faceSize << " " << grid << " " << hists.size() << " " << featLen << "\n";
        for (size_t i = 0; i < hists.size(); ++i) {
            f << labels[i];
            for (float v : hists[i]) f << " " << v;
            f << "\n";
        }
        return true;
    }

    bool carregar(const std::string& caminho) {
        std::ifstream f(caminho);
        if (!f) return false;
        size_t count, featLen;
        f >> faceSize >> grid >> count >> featLen;
        hists.assign(count, std::vector<float>(featLen, 0.f));
        labels.assign(count, 0);
        for (size_t i = 0; i < count; ++i) {
            f >> labels[i];
            for (size_t j = 0; j < featLen; ++j) f >> hists[i][j];
        }
        return true;
    }
};

inline double fftRatio(const cv::Mat& gray120)
{
    cv::Mat small;
    cv::resize(gray120, small, cv::Size(60, 60));
    cv::Mat f; small.convertTo(f, CV_32F);

    cv::Mat planes[] = { f, cv::Mat::zeros(f.size(), CV_32F) };
    cv::Mat complexI;
    cv::merge(planes, 2, complexI);
    cv::dft(complexI, complexI);
    cv::split(complexI, planes);

    cv::Mat mag;
    cv::magnitude(planes[0], planes[1], mag);
    mag += 1.0f;
    cv::log(mag, mag);
    mag *= 20.0f;

    int cx = mag.cols / 2, cy = mag.rows / 2;
    cv::Mat q0(mag, cv::Rect(0,  0,  cx, cy));
    cv::Mat q1(mag, cv::Rect(cx, 0,  cx, cy));
    cv::Mat q2(mag, cv::Rect(0,  cy, cx, cy));
    cv::Mat q3(mag, cv::Rect(cx, cy, cx, cy));
    cv::Mat tmp;
    q0.copyTo(tmp); q3.copyTo(q0); tmp.copyTo(q3);
    q1.copyTo(tmp); q2.copyTo(q1); tmp.copyTo(q2);

    double somaLow = 0, nLow = 0, somaHigh = 0, nHigh = 0;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            double dx = x - 30, dy = y - 30;
            double dist = std::sqrt(dx*dx + dy*dy);
            double val  = mag.at<float>(y, x);
            if (dist <= 8)                  { somaLow  += val; nLow++; }
            else if (dist > 8 && dist <= 25){ somaHigh += val; nHigh++; }
        }
    }
    double eLow  = (nLow  > 0) ? somaLow  / nLow  : 0;
    double eHigh = (nHigh > 0) ? somaHigh / nHigh : 0;
    return eHigh / (eLow + 1e-10);
}

}
#endif
