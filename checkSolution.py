#!/usr/bin/env python3

'''
Purpose:
    Check the closeness of computed solution to real answer
    Computed by np.linalg.solve or np.linalg.lstsq

Command line argument:
    ifname: Input filename to the solver
    ofname: Output filename generated by solver

Console output:
    Frobenius norm of x - x_hat
'''

import sys
import numpy as np

def readInputFile(ifname):
    content = []
    with open(ifname) as f:
        content = f.readlines()
    content = [x.strip() for x in content]

    n = int(content[0])
    A = []
    for i in range(n):
        A.append([float(x) for x in content[i + 1].split()])
    b = [float(x) for x in content[n + 1].split()]

    return A, b

def computeLaplacian(A):
    d = [sum(row) for row in A]
    return np.diag(d) - np.array(A)

def computeSolution(A, b):
    L = np.array(computeLaplacian(A))
    b = np.array(b)
    x = np.linalg.solve(L, b)
    assert np.allclose(np.dot(L, x), b)
    return x

def readOutputFile(ofname):
    content = []
    with open(ofname) as f:
        content = f.readlines()
    content = [x.strip() for x in content]
    return [float(i) for i in content[0].split()]

def align(x, x_hat):
    return x - np.min(x), x_hat - np.min(x_hat)

if __name__ == "__main__":
    ifname = sys.argv[1]
    A, b = readInputFile(ifname)

    x = computeSolution(A, b)

    ofname = sys.argv[2]
    x_hat = readOutputFile(ofname)

    x, x_hat = align(x, x_hat)
    print(x)
    print(x_hat)
    print(np.linalg.norm(x - x_hat)/np.linalg.norm(x))

