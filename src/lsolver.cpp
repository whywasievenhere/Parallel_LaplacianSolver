#include "rng.h"
#include "lsolver.h"

#include <omp.h>
#include <math.h>
#include <assert.h>

#include <bitset>
#include <chrono>
#include <numeric>
#include <iostream>
#include <algorithm>

template <typename T>
inline T max(const std::vector<T>& a) {
    return *std::max_element(a.begin(), a.end());
}

template <typename T>
inline T sum(const std::vector<T>& a) {
    return std::accumulate(a.begin(), a.end(), (T) 0);
}

void Lsolver::initGraph(const Graph& g) {
    n = g.getNumVertex();
    d = g.getDegreeMatrix();

    adj.resize(n), sampler.reserve(n);
    for (int i = 0; i < n; ++i) {
        adj[i] = g.getNeighbors(i);
        for (auto& j: adj[i]) {
            j.second /= d[i];
        }
        sampler.push_back(Sampler(adj[i]));
    }
}

void Lsolver::computeJ(const std::vector<double>& b) {
    b_sink = b.back();
    J.resize(n);
#pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        J[i] = -b[i]/b_sink;
    }
}

std::vector<double> Lsolver::solve() {
    auto start = std::chrono::steady_clock::now();

    computeStationarityState();
    auto x = computeX();

    auto finish = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration_cast<
        std::chrono::duration<double> >(finish - start).count();

    std::cerr << "Time: " << elapsed_seconds << '\n';
    return x;
}

inline bool trueWithProbability(double p) {
    return random_double <= p;
}

#ifndef N_THREADS
#define N_THREADS 16
#endif

#define LENGTH_OF_EPOCH 2000

void Lsolver::pll_v1() {
    std::vector<int> inQ[N_THREADS];
#pragma omp parallel
    {
        int tid = omp_get_thread_num();
        inQ[tid].resize(n, 0);
        for (int t = 0; t < LENGTH_OF_EPOCH; ++t) {

#pragma omp for
            for (int i = 0; i < n - 1; ++i) {
                Q[i] += trueWithProbability(beta * J[i]);
                for (int cap = std::max(1, Q[i]/2); Q[i] and cap; --cap) {
                    --Q[i];
                    ++cnt[i];
                    ++inQ[tid][sampler[i].generate()];
                }
            }

#pragma omp for
            for (int i = 0; i < n; ++i) {
                int x = 0;
                for (int p = 0; p < N_THREADS; ++p) {
                    x += inQ[p][i], inQ[p][i] = 0;
                }
                Q[i] += x;
            }
        }
    }
}

#define K 64
void Lsolver::pll_v2() {
    std::vector<int> inQ[N_THREADS];
    std::vector< std::bitset<K> > via[N_THREADS];
#pragma omp parallel
    {
        int tid = omp_get_thread_num();
        inQ[tid].resize(n, 0), via[tid].resize(n);
        for (int t = 0; t < LENGTH_OF_EPOCH; ++t) {

#pragma omp for
            for (int i = 0; i < n - 1; ++i) {
                Q[i] += trueWithProbability(beta * J[i]);
                if (Q[i]) {
                    --Q[i];
                    int j = i;
                    for (int k = 0; k < K and j != n - 1; ++k) {
                        via[tid][j].set(k);
                        j = sampler[j].generate();
                    }
                    ++inQ[tid][j];
                }
            }

#pragma omp for
            for (int i = 0; i < n; ++i) {
                int x = 0;
                std::bitset<K> b;
                for (int p = 0; p < N_THREADS; ++p) {
                    x += inQ[p][i], inQ[p][i] = 0;
                    b |= via[p][i], via[p][i] = 0;
                }
                Q[i] += x;
                cnt[i] += b.count();
            }
        }
    }
}

void Lsolver::serial() {
    std::vector<int> inQ(n, 0);
    for (int t = 0; t < LENGTH_OF_EPOCH; ++t) {
        for (int i = 0; i < n - 1; ++i) {
            Q[i] += trueWithProbability(beta * J[i]);
            if (Q[i] > 0) {
                --Q[i];
                ++cnt[i];
                ++inQ[sampler[i].generate()];
            }
        }
        for (int i = 0; i < n; ++i) {
            Q[i] += inQ[i];
            inQ[i] = 0;
        }
    }
}

const double EPS = 1e-3;
const int MIN_EPOCHS = 3;
const int MAX_EPOCHS = 100;

double Lsolver::estimateEta(double lastC = 0) {
    int epoch = 0;
    double newC = 0;

    while (epoch < MIN_EPOCHS and newC < 0.8) {
        ++epoch;
        pll_v1();
        newC = (double) Q[n - 1]/(1 + sum(Q));
    }

    for (int thres = 0; thres < 3 and epoch < MAX_EPOCHS; ++epoch) {
        auto oldC = newC;

        pll_v1();
        newC = (double) Q[n - 1]/(1 + sum(Q));

        if (beta > 0.001 and fabs(oldC - newC) < EPS) break;
        thres += (newC > 0.90);
    }

    int T = epoch * LENGTH_OF_EPOCH;
    for (int i = 0; i < n; ++i) {
        eta[i] = (double) cnt[i]/T;
        Q[i] = 0, cnt[i] = 0;
    }
    return newC;
}

void Lsolver::computeStationarityState() {
    omp_set_num_threads(N_THREADS);

    // Can start with any big value, but beta < beta* is below 1
    beta = 0.1;
    double C = 0;

    eta.resize(n, 0), Q.resize(n, 0), cnt.resize(n, 0);
    do {
        beta /= 2;
        C = estimateEta(C);
    } while (C < 0.85 and beta >= 0.0001);
}

std::vector<double> Lsolver::computeX() {
    std::vector<double> x(n);
#pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        x[i] = (-b_sink/beta) * (eta[i]/d[i]);
    }
    return x;

    // centering for canonical solution
    auto avg_x = sum(x)/n;
#pragma omp parallel for
    for (int i = 0; i < n; ++i) {
        x[i] -= avg_x;
    }
}
