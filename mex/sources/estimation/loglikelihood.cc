/*
 * Copyright (C) 2010 Dynare Team
 *
 * This file is part of Dynare.
 *
 * Dynare is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Dynare is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Dynare.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include "Vector.hh"
#include "Matrix.hh"
#include "LogLikelihoodMain.hh"

#include "mex.h"

void
fillEstParamsInfo(const mxArray *estim_params_info, EstimatedParameter::pType type,
                  std::vector<EstimatedParameter> &estParamsInfo)
{
  size_t m = mxGetM(estim_params_info), n = mxGetN(estim_params_info);
  MatrixConstView epi(mxGetPr(estim_params_info), m, n, m);
  for (size_t i = 0; i < m; i++)
    {
      size_t col = 0;
      size_t id1 = (size_t) epi(i, col++) - 1;
      size_t id2 = 0;
      if (type == EstimatedParameter::shock_Corr
          || type == EstimatedParameter::measureErr_Corr)
        id2 = (size_t) epi(i, col++) - 1;
      col++; // Skip init_val
      double low_bound = epi(i, col++);
      double up_bound = epi(i, col++);
      Prior::pShape shape = (Prior::pShape) epi(i, col++);
      double mean = epi(i, col++);
      double std = epi(i, col++);
      double p3 = epi(i, col++);
      double p4 = epi(i, col++);

      //      Prior *p = Prior::constructPrior(shape, mean, std, low_bound, up_bound, p3, p4);
      Prior *p = NULL;

      // Only one subsample
      std::vector<size_t> subSampleIDs;
      subSampleIDs.push_back(0);

      estParamsInfo.push_back(EstimatedParameter(type, id1, id2, subSampleIDs,
                                                 low_bound, up_bound, p));
    }
}

double
loglikelihood(const VectorConstView &estParams, const MatrixConstView &data,
              const std::string &mexext)
{
  // Retrieve pointers to global variables
  const mxArray *M_ = mexGetVariablePtr("global", "M_");
  const mxArray *oo_ = mexGetVariablePtr("global", "oo_");
  const mxArray *options_ = mexGetVariablePtr("global", "options_");
  const mxArray *estim_params_ = mexGetVariablePtr("global", "estim_params_");

  // Construct arguments of constructor of LogLikelihoodMain
  char *fName = mxArrayToString(mxGetField(M_, 0, "fname"));
  std::string dynamicDllFile(fName);
  mxFree(fName);
  dynamicDllFile += "_dynamic" + mexext;

  size_t n_endo = (size_t) *mxGetPr(mxGetField(M_, 0, "endo_nbr"));
  size_t n_exo = (size_t) *mxGetPr(mxGetField(M_, 0, "exo_nbr"));
  size_t n_param = (size_t) *mxGetPr(mxGetField(M_, 0, "param_nbr"));
  size_t n_estParams = estParams.getSize();

  std::vector<size_t> zeta_fwrd, zeta_back, zeta_mixed, zeta_static;
  const mxArray *lli_mx = mxGetField(M_, 0, "lead_lag_incidence");
  MatrixConstView lli(mxGetPr(lli_mx), mxGetM(lli_mx), mxGetN(lli_mx), mxGetM(lli_mx));
  if (lli.getRows() != 3 || lli.getCols() != n_endo)
    mexErrMsgTxt("Incorrect lead/lag incidence matrix");
  for (size_t i = 0; i < n_endo; i++)
    {
      if (lli(0, i) == 0 && lli(2, i) == 0)
        zeta_static.push_back(i);
      else if (lli(0, i) != 0 && lli(2, i) == 0)
        zeta_back.push_back(i);
      else if (lli(0, i) == 0 && lli(2, i) != 0)
        zeta_fwrd.push_back(i);
      else
        zeta_mixed.push_back(i);
    }

  double qz_criterium = *mxGetPr(mxGetField(options_, 0, "qz_criterium"));
  double lyapunov_tol = *mxGetPr(mxGetField(options_, 0, "lyapunov_complex_threshold"));
  double riccati_tol = *mxGetPr(mxGetField(options_, 0, "riccati_tol"));

  std::vector<size_t> varobs;
  const mxArray *varobs_mx = mxGetField(options_, 0, "varobs_id");
  if (mxGetM(varobs_mx) != 1)
    mexErrMsgTxt("options_.varobs_id must be a row vector");
  size_t n_varobs = mxGetN(varobs_mx);
  std::transform(mxGetPr(varobs_mx), mxGetPr(varobs_mx) + n_varobs, back_inserter(varobs),
                 std::bind2nd(std::minus<size_t>(), 1));

  if (data.getRows() != n_varobs)
    mexErrMsgTxt("Data has not as many rows as there are observed variables");
  
  std::vector<EstimationSubsample> estSubsamples;
  estSubsamples.push_back(EstimationSubsample(0, data.getCols() - 1));

  std::vector<EstimatedParameter> estParamsInfo;
  fillEstParamsInfo(mxGetField(estim_params_, 0, "var_exo"), EstimatedParameter::shock_SD,
                    estParamsInfo);
  fillEstParamsInfo(mxGetField(estim_params_, 0, "var_endo"), EstimatedParameter::measureErr_SD,
                    estParamsInfo);
  fillEstParamsInfo(mxGetField(estim_params_, 0, "corrx"), EstimatedParameter::shock_Corr,
                    estParamsInfo);
  fillEstParamsInfo(mxGetField(estim_params_, 0, "corrn"), EstimatedParameter::measureErr_Corr,
                    estParamsInfo);
  fillEstParamsInfo(mxGetField(estim_params_, 0, "param_vals"), EstimatedParameter::deepPar,
                    estParamsInfo);

  EstimatedParametersDescription epd(estSubsamples, estParamsInfo);

  // Allocate LogLikelihoodMain object
  int info;
  LogLikelihoodMain llm(dynamicDllFile, epd, n_endo, n_exo, zeta_fwrd, zeta_back, zeta_mixed, zeta_static,
                        qz_criterium, varobs, riccati_tol, lyapunov_tol, info);

  // Construct arguments of compute() method
  Matrix steadyState(n_endo, 1);
  mat::get_col(steadyState, 0) = VectorConstView(mxGetPr(mxGetField(oo_, 0, "steady_state")), n_endo, 1);

  Vector estParams2(n_estParams);
  estParams2 = estParams;
  Vector deepParams(n_param);
  deepParams = VectorConstView(mxGetPr(mxGetField(M_, 0, "params")), n_param, 1);
  Matrix Q(n_exo);
  Q = MatrixConstView(mxGetPr(mxGetField(M_, 0, "Sigma_e")), n_exo, n_exo, 1);
  Matrix H(n_varobs);
  const mxArray *H_mx = mxGetField(M_, 0, "H");
  if (mxGetM(H_mx) == 1 && mxGetN(H_mx) == 1 && *mxGetPr(H_mx) == 0)
    H.setAll(0.0);
  else
    H = MatrixConstView(mxGetPr(mxGetField(M_, 0, "H")), n_varobs, n_varobs, 1);

  // Compute the likelihood
  double lik = llm.compute(steadyState, estParams2, deepParams, data, Q, H, 0, info);

  // Cleanups
  /*
  for (std::vector<EstimatedParameter>::iterator it = estParamsInfo.begin();
       it != estParamsInfo.end(); it++)
    delete it->prior;
  */

  return lik;
}

void
mexFunction(int nlhs, mxArray *plhs[],
            int nrhs, const mxArray *prhs[])
{
  if (nrhs != 3)
    mexErrMsgTxt("loglikelihood: exactly three arguments are required.");
  if (nlhs != 1)
    mexErrMsgTxt("loglikelihood: exactly one return argument is required.");

  // Check and retrieve the arguments

  if (!mxIsDouble(prhs[0]) || mxGetN(prhs[0]) != 1)
    mexErrMsgTxt("First argument must be a column vector of double-precision numbers");

  VectorConstView estParams(mxGetPr(prhs[0]), mxGetM(prhs[0]), 1);

  if (!mxIsDouble(prhs[1]))
    mexErrMsgTxt("Second argument must be a matrix of double-precision numbers");

  MatrixConstView data(mxGetPr(prhs[1]), mxGetM(prhs[1]), mxGetN(prhs[1]), mxGetM(prhs[1]));

  if (!mxIsChar(prhs[2]))
    mexErrMsgTxt("Third argument must be a character string");

  char *mexext_mx = mxArrayToString(prhs[2]);
  std::string mexext(mexext_mx);
  mxFree(mexext_mx);

  // Compute and return the value
  double lik = loglikelihood(estParams, data, mexext);

  plhs[0] = mxCreateDoubleMatrix(1, 1, mxREAL); 
  *mxGetPr(plhs[0]) = lik;
}
