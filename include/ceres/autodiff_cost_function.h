// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2010, 2011, 2012 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: sameeragarwal@google.com (Sameer Agarwal)
//
// Helpers for making CostFunctions as needed by the least squares framework,
// with Jacobians computed via automatic differentiation. For more information
// on automatic differentation, see the wikipedia article at
// http://en.wikipedia.org/wiki/Automatic_differentiation
//
// To get an auto differentiated cost function, you must define a class with a
// templated operator() (a functor) that computes the cost function in terms of
// the template parameter T. The autodiff framework substitutes appropriate
// "jet" objects for T in order to compute the derivative when necessary, but
// this is hidden, and you should write the function as if T were a scalar type
// (e.g. a double-precision floating point number).
//
// The function must write the computed value in the last argument (the only
// non-const one) and return true to indicate success.
//
// For example, consider a scalar error e = k - x'y, where both x and y are
// two-dimensional column vector parameters, the prime sign indicates
// transposition, and k is a constant. The form of this error, which is the
// difference between a constant and an expression, is a common pattern in least
// squares problems. For example, the value x'y might be the model expectation
// for a series of measurements, where there is an instance of the cost function
// for each measurement k.
//
// The actual cost added to the total problem is e^2, or (k - x'k)^2; however,
// the squaring is implicitly done by the optimization framework.
//
// To write an auto-differentiable cost function for the above model, first
// define the object
//
//   class MyScalarCostFunction {
//     MyScalarCostFunction(double k): k_(k) {}
//
//     template <typename T>
//     bool operator()(const T* const x , const T* const y, T* e) const {
//       e[0] = T(k_) - x[0] * y[0] + x[1] * y[1];
//       return true;
//     }
//
//    private:
//     double k_;
//   };
//
// Note that in the declaration of operator() the input parameters x and y come
// first, and are passed as const pointers to arrays of T. If there were three
// input parameters, then the third input parameter would come after y. The
// output is always the last parameter, and is also a pointer to an array. In
// the example above, e is a scalar, so only e[0] is set.
//
// Then given this class definition, the auto differentiated cost function for
// it can be constructed as follows.
//
//   CostFunction* cost_function
//       = new AutoDiffCostFunction<MyScalarCostFunction, 1, 2, 2>(
//           new MyScalarCostFunction(1.0));              ^  ^  ^
//                                                        |  |  |
//                            Dimension of residual ------+  |  |
//                            Dimension of x ----------------+  |
//                            Dimension of y -------------------+
//
// In this example, there is usually an instance for each measumerent of k.
//
// In the instantiation above, the template parameters following
// "MyScalarCostFunction", "1, 2, 2", describe the functor as computing a
// 1-dimensional output from two arguments, both 2-dimensional.
//
// The framework can currently accommodate cost functions of up to 6 independent
// variables, and there is no limit on the dimensionality of each of them.
//
// WARNING #1: Since the functor will get instantiated with different types for
// T, you must to convert from other numeric types to T before mixing
// computations with other variables of type T. In the example above, this is
// seen where instead of using k_ directly, k_ is wrapped with T(k_).
//
// WARNING #2: A common beginner's error when first using autodiff cost
// functions is to get the sizing wrong. In particular, there is a tendency to
// set the template parameters to (dimension of residual, number of parameters)
// instead of passing a dimension parameter for *every parameter*. In the
// example above, that would be <MyScalarCostFunction, 1, 2>, which is missing
// the last '2' argument. Please be careful when setting the size parameters.

#ifndef CERES_PUBLIC_AUTODIFF_COST_FUNCTION_H_
#define CERES_PUBLIC_AUTODIFF_COST_FUNCTION_H_

#include <glog/logging.h>
#include "ceres/internal/autodiff.h"
#include "ceres/internal/scoped_ptr.h"
#include "ceres/sized_cost_function.h"

namespace ceres {

// A cost function which computes the derivative of the cost with respect to the
// parameters (a.k.a. the jacobian) using an autodifferentiation framework. The
// first template argument is the functor object, described in the header
// comment. The second argument is the dimension of the residual, and subsequent
// arguments describe the size of the Nth parameter, one per parameter.
//
// The constructor, which takes a cost functor, takes ownership of the functor.
template <typename CostFunctor,
          int M,        // Number of residuals.
          int N0,       // Number of parameters in block 0.
          int N1 = 0,   // Number of parameters in block 1.
          int N2 = 0,   // Number of parameters in block 2.
          int N3 = 0,   // Number of parameters in block 3.
          int N4 = 0,   // Number of parameters in block 4.
          int N5 = 0>   // Number of parameters in block 5.
class AutoDiffCostFunction :
  public SizedCostFunction<M, N0, N1, N2, N3, N4, N5> {
 public:
  // Takes ownership of functor.
  explicit AutoDiffCostFunction(CostFunctor* functor) : functor_(functor) {}

  virtual ~AutoDiffCostFunction() {}

  // Implementation details follow; clients of the autodiff cost function should
  // not have to examine below here.
  //
  // To handle varardic cost functions, some template magic is needed. It's
  // mostly hidden inside autodiff.h.
  virtual bool Evaluate(double const* const* parameters,
                        double* residuals,
                        double** jacobians) const {
    if (!jacobians) {
      return internal::VariadicEvaluate<
          CostFunctor, double, N0, N1, N2, N3, N4, N5>
          ::Call(*functor_, parameters, residuals);
    }
    return internal::AutoDiff<CostFunctor, double,
           M, N0, N1, N2, N3, N4, N5>::Differentiate(*functor_,
                                                     parameters,
                                                     residuals,
                                                     jacobians);
  }

 private:
  internal::scoped_ptr<CostFunctor> functor_;
};

}  // namespace ceres

#endif  // CERES_PUBLIC_AUTODIFF_COST_FUNCTION_H_
