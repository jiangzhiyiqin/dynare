function y_=simult(ys, dr)
% function y_=simult(ys, dr)
% Recursive Monte Carlo simulations
%
% INPUTS
%    ys:    vector of variables in steady state
%    dr:    structure of decisions rules for stochastic simulations
%
% OUTPUTS
%    y_:    stochastic simulations results
%
% SPECIAL REQUIREMENTS
%    none
%  

% Copyright (C) 2001-2007 Dynare Team
%
% This file is part of Dynare.
%
% Dynare is free software: you can redistribute it and/or modify
% it under the terms of the GNU General Public License as published by
% the Free Software Foundation, either version 3 of the License, or
% (at your option) any later version.
%
% Dynare is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
% GNU General Public License for more details.
%
% You should have received a copy of the GNU General Public License
% along with Dynare.  If not, see <http://www.gnu.org/licenses/>.

global M_ options_ oo_
global  it_ means_

order = options_.order;
replic = options_.replic;
if replic == 0
  replic = 1;
end
seed = options_.simul_seed;
options_.periods = options_.periods;

it_ = M_.maximum_lag + 1 ;

if replic > 1
  fname = [M_.fname,'_simul'];
  fh = fopen(fname,'w+');
end

% eliminate shocks with 0 variance
i_exo_var = setdiff([1:M_.exo_nbr],find(diag(M_.Sigma_e) == 0));
nxs = length(i_exo_var);
oo_.exo_simul = zeros(M_.maximum_lag+M_.maximum_lead+options_.periods,M_.exo_nbr);
chol_S = chol(M_.Sigma_e(i_exo_var,i_exo_var));

for i=1:replic
  if isempty(seed)
    randn('state',sum(100*clock));
  else
    randn('state',seed+i-1);
  end
  if ~isempty(M_.Sigma_e)
    oo_.exo_simul(:,i_exo_var) = randn(M_.maximum_lag+M_.maximum_lead+options_.periods,nxs)*chol_S;
  end
  y_ = simult_(ys,dr,oo_.exo_simul,order);
  if replic > 1
    fwrite(fh,oo_.endo_simul(:,M_.maximum_lag+1:end),'float64');
  end
end

if replic > 1
  fclose(fh);
end


% 02/20/01 MJ replaced ys by dr.ys
% 02/22/01 MJ removed commented out lines
%             removed useless temps
%             stderr_ replaced by M_.Sigma_e
% 02/28/01 MJ changed expression for M_.Sigma_e
% 02/18/03 MJ added ys in the calling sequence for arbitrary initial values
%             suppressed useless calling parameter istoch
% 05/10/03 MJ removed repmat() in call to simult_() for lag > 1
% 05/29/03 MJ test for 0 variances
