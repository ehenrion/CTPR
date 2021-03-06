/******************************************************************************
* File: CPRSppMain.cpp
* Version: CPR and CPRMPI 1.0
* Author: Wonil Chung
* First Revised: 02.14.2014
* Last Revised:  11.07.2016 
* Description: C++ Code for Cross-Trait Penalized Regression Model 
*  using a Single Process 
******************************************************************************/


#include <RcppArmadillo.h>
#include <Rcpp.h>
#include "CPRCppSrc.cpp"


using namespace Rcpp; 


/*************************************************************************/
/*                                                                       */
/* Single Process Code                                                   */
/*                                                                       */
/*************************************************************************/

// Main procedure
void CPRSppProc(arma::field<arma::fvec> & Ys, arma::field<arma::fmat> & Xs, const arma::fcube & A, arma::field<arma::fvec> & lambda1, arma::fvec lambda2, 
	arma::field<arma::fmat> & b_est, arma::field<arma::fmat> & b_est_2, arma::field<arma::fmat> & b_re_est, arma::fmat & b_sec, arma::umat & nzero, PARAM & cPar) {

	// Determine a sparcity penaty term (Lasso vs MCP)
	void (* _Cycle) (const arma::field<arma::fvec> &, const arma::field<arma::fmat> &,
		arma::fmat &, arma::fmat &, const arma::umat &, const arma::fcube &, int, int, arma::uvec, float, float, const arma::field<arma::fvec> &, bool, float);

	if (cPar.penalize == "MCP" && cPar.penalize2 == "CTPR" && cPar.useSummary == 0) {
		_Cycle = & _Cycle_M;
		std::cout << "MCP and CTPR penalties are used..." << std::endl;
	} else if (cPar.penalize == "Lasso" && cPar.penalize2 == "CTPR" && cPar.useSummary == 0){
		_Cycle = & _Cycle_L;
		std::cout << "Lasso and CTPR penalties are used..." << std::endl;
	} else if (cPar.penalize == "MCP" && cPar.penalize2 == "CTPR" && cPar.useSummary == 1) {
		_Cycle = & _Cycle_MS;
		std::cout << "MCP and CTPR penalties are used with Summary Statistics..." << std::endl;
	} else if (cPar.penalize == "Lasso" && cPar.penalize2 == "CTPR" && cPar.useSummary == 1){
		_Cycle = & _Cycle_LS;
		std::cout << "Lasso and CTPR penalties are used with Summary Statistics..." << std::endl;
	} 

	int maxNz = cPar.maxNz;
	bool re_est = cPar.re_est;
	float tol = cPar.tol; 
	float gamma = cPar.gamma; 
	int maxIter = cPar.maxIter; 

	int i, j;
	int n = Ys(0).n_elem;
	int m = Ys.n_elem;
	int p = Xs(0).n_cols;
	int q = lambda2.n_elem;
	arma::uvec ns(m);
	b_est.set_size(q);
	b_est_2.set_size(q);

	// Retrieve the centers and Center X and Y
	arma::fmat Xs_c(m, p);
	arma::fmat Xs_s(m, p);
	arma::fvec Ys_c(m);
	for (i=0; i<m; i++) {
		ns(i) = Xs(i).n_rows;
		Xs_c.row(i) = arma::mean(Xs(i));
		Xs_s.row(i) = arma::stddev(Xs(i), 1);
		Xs(i).each_row() -= Xs_c.row(i);
		Xs(i).each_row() /= Xs_s.row(i);
		Ys_c(i) = arma::mean(Ys(i));
		Ys(i) -= Ys_c(i);
	}

	////////////////////////////////////////////////////////////////////
	bool bGWAS = FALSE;
	arma::field<arma::fvec> Xsb(m);
	for (i=0; i<m; i++) Xsb(i) = arma::zeros<arma::fvec>(Ys(i).n_elem);
	////////////////////////////////////////////////////////////////////

	// Determine the maximum lambda1
	if (lambda1.n_elem == 0) {
		std::cout << "Determining maximum lambda1..." << std::endl;
		float lambda1_max = _MaxLambda(Ys, Xs, ns, 1, p);
		lambda1.set_size(q);
		for (i=0; i<q; i++) {
			lambda1(i) = lambda1_max * arma::exp(-6.907755 * arma::linspace<arma::fvec>(0, 1, 101)); 
		}
		//lambda1(0).save("/n/home03/wchung/gtex/simulation/150101/lambda1.txt", arma::csv_ascii);
		//std::cout << lambda1_max << std::endl;
		//std::cout << lambda1(0)(0) << lambda1(0)(1) << lambda1(0)(100) << std::endl;
	} else {
		if (lambda1.n_elem == 1 && q > 1) {
			arma::fvec tempt = lambda1(0);
			lambda1.set_size(q);
			for (i=0; i<q; i++) {
				lambda1(i) = tempt;
			}
		} else {
			if (lambda1.n_elem != q) {
				std::cout << "The list of lambda1 should have the same length as lambda2..." << std::endl;
				return;
			}
		}
	}
	
	int iter, flag = 0;
	float _lambda1, _lambda2; 
	arma::fmat _b_est_v1(m, p);
	arma::fmat _b_est_v2(m, p);
	arma::fmat _b_est_st(m, p);
  arma::umat active_set(m, p);
  arma::umat active_set2(m, p);
  arma::frowvec temp2(p+1);

  int L1 = 0;
  int L2 = q;
  for (i=0; i<lambda1.n_elem; i++) {
  	L1 = lambda1(i).n_elem > L1 ? lambda1(i).n_elem : L1;
  }
  arma::fmat RSS(L1, L2);
  RSS.fill(NA_REAL);
	
	// Conduct Coordinate Descent Algorithm
  for (j=0; j<q; j++) {
  	std::cout << "*";
  	arma::fmat _b_est(lambda1(j).n_elem, p+1);

	//std::cout << lambda2(j) << std::endl;
	//std::cout << A.slice(0)(0,0) << " " << A.slice(0)(0,1) << " " << A.slice(0)(1,0) << " " << A.slice(0)(1,1) << std::endl;
	//std::cout << A.slice(1)(0,0) << " " << A.slice(1)(0,1) << " " << A.slice(1)(1,0) << " " << A.slice(1)(1,1) << std::endl;
	//std::cout << m << " " << p << " " << ns << " " << gamma << std::endl;

  	_lambda2 = lambda2(j);
		std::cout << lambda1(j).n_elem;
  	for (i=0; i<lambda1(j).n_elem; i++) {
			std::cout << "~";
      _lambda1 = lambda1(j)(i);
      if (i == 0) { // Basically, initial values for all beta are zero
      	_b_est_v1.zeros();
      } else {
      	_b_est_v1 = _b_est_st;
      }

      // One complete cycle through all the coefficients to determine the active sets
      _Cycle(Ys, Xs, _b_est_v1, b_sec, arma::ones<arma::umat>(m, p), A, m, p, ns, _lambda1, _lambda2, Xsb, bGWAS, gamma);

		//if (i==0) {
		//std::cout << _b_est_v1(0,0) << " " << _b_est_v1(0,1) << " " << _b_est_v1(0,2) << std::endl;
		//std::cout << _b_est_v1(1,0) << " " << _b_est_v1(1,1) << " " << _b_est_v1(1,2) << std::endl;
		//}

			// Iterate on only the active set till convergence
      iter = 0;
      while (TRUE) {
      	active_set = (arma::abs(_b_est_v1) > arma::datum::eps);
      	_b_est_v2.ones();
      	while (TRUE)  {
      		iter += 1;
      		if (arma::max(arma::max(arma::abs(_b_est_v1 - _b_est_v2))) <= tol) break;
      		_b_est_v2 = _b_est_v1;
      		if (iter >= maxIter) {
      			std::cout << "Exceeds the maximum iteration for lambda:" << _lambda1 <<"," << _lambda2;
      			std::cout << ". Further lambda1 will not be considered." << std::endl;
      			flag = 1;
      			break;
      		}
      		_Cycle(Ys, Xs, _b_est_v1, b_sec, active_set, A, m, p, ns, _lambda1, _lambda2, Xsb, bGWAS, gamma);
      	}

      	if (flag == 1) break;
      	_Cycle(Ys, Xs, _b_est_v1, b_sec, arma::ones<arma::umat>(m, p), A, m, p, ns, _lambda1, _lambda2, Xsb, bGWAS, gamma);
      	active_set2 = (arma::abs(_b_est_v1) > arma::datum::eps);
      	if (arma::accu(active_set2 != active_set) < 0.5) break;
      }

      if (flag == 1) {
      	flag = 0;
      	break;
      }

      if (arma::sum(arma::abs(_b_est_v1.row(0)) > arma::datum::eps) > maxNz) {
				break;
			}

      // Convert to the original scale including the intercept
      // Recording only the primary phenotype
      RSS(i, j) = arma::sum(arma::square(Ys(0) - Xs(0) * _b_est_v1.row(0).t()));

      temp2(arma::span(1, p)) = _b_est_v1.row(0) / Xs_s.row(0);
      temp2(0) = Ys_c(0) - arma::sum(_b_est_v1.row(0) / Xs_s.row(0) % Xs_c.row(0));
      _b_est.row(i) = temp2;
			_b_est_st = _b_est_v1;

		}
    _b_est.resize(i, p+1);
		b_est(j) = _b_est;
    
    lambda1(j) = lambda1(j)(arma::span(0, i-1));
    std::cout << std::endl;
  }

  // Calculate AIC, BIC, GCV
  L1 = 0;
  for (i=0; i<lambda1.n_elem; i++) {
  	L1 = lambda1(i).n_elem > L1 ? lambda1(i).n_elem : L1;
  }
  nzero.set_size(L1, L2);
  RSS.resize(L1, L2);

  nzero.fill(NA_INTEGER);

  for (j=0; j<L2; j++) {
  	for (i=0; i<lambda1(j).n_elem; i++) {
  		nzero(i, j) = arma::sum(arma::abs(b_est(j).row(i)) > arma::datum::eps) - 1;
  	}
  }

  // Re-estimation of the coefficients
	if (re_est == TRUE) {
		arma::urowvec active_set3;
	  arma::frowvec _b_re_est_v1;
	  arma::fmat _b_re_est;
		arma::fmat RSS2(L1, L2);
		RSS2.fill(NA_REAL);
		b_re_est.set_size(q);
		for (j=0; j<q; j++) {
			_b_re_est.set_size(b_est(j).n_rows, p+1);
			for (i=0; i<b_est(j).n_rows; i++) {
				active_set3 = (arma::abs(b_est(j)(i, arma::span(1, p))) > arma::datum::eps);
				_Re_Estimate(Ys(0), Xs(0), _b_re_est_v1, active_set3);
				RSS2(i, j) = arma::sum(arma::square(Ys(0) - Xs(0) * _b_re_est_v1.t()));
				temp2(arma::span(1, p)) = _b_re_est_v1 / Xs_s.row(0);
				temp2(0) = Ys_c(0) - arma::sum(_b_re_est_v1 / Xs_s.row(0) % Xs_c.row(0));
				_b_re_est.row(i) = temp2;
			}
			b_re_est(j) = _b_re_est;
		}
	}

	// change to normal scale for genotype and phenotype data
	for (i=0; i<m; i++) {
		Xs(i).each_row() %= Xs_s.row(i);
		Xs(i).each_row() += Xs_c.row(i);
		Ys(i) += Ys_c(i);
	}
	/////////////////////////////////////////////////////////

	std::cout << "Finished!" << std::endl;    
	return;
}


// Compute MSE using single node
void CPRSppMSE(arma::fmat & mse, arma::field<arma::fmat> & b_est, arma::field<arma::fvec> & lambda1, 
	arma::fvec lambda2, int L1, int L2, arma::fvec & Ys_test, arma::fmat & Xs_test, arma::fmat & cvR2){
	int i, j;
	
	for (i=0; i<L2; i++) {
		for (j=0; j<lambda1(i).n_elem; j++) {
			mse(j, i) = arma::mean(arma::square(Ys_test - Xs_test * b_est(i).row(j).t()));
			cvR2(j, i) = pow(arma::as_scalar(arma::cor(Ys_test, Xs_test * b_est(i).row(j).t())),2);
			//cvR2(j, i) = 1.0-mse(j, i)/arma::mean(arma::square(Ys_test-mean(Ys_test))); ///// 
		}
	}
}

 
// Main procedure
void CPRSppTune(arma::field<arma::fvec> & Ys, arma::field<arma::fmat> & Xs, const arma::fcube & A,
	arma::field<arma::fmat> & b_est, arma::field<arma::fmat> & b_est_2, arma::field<arma::fmat> & b_re_est, arma::fmat & b_sec, 
	arma::fvec & b_min, arma::fvec & b_min0, arma::fmat & cvm, arma::fvec & cprRes, PARAM & cPar) {

	std::cout << "-------------------------------------------------------------" << std::endl;
	std::cout << "               COORDINATE DECENT ALGORITHM                   " << std::endl;
	std::cout << "-------------------------------------------------------------" << std::endl;
	std::cout << std::endl;
	
	int maxNz = cPar.maxNz;
	bool re_est = cPar.re_est;
	float tol = cPar.tol; 
	float gamma = cPar.gamma; 
	int maxIter = cPar.maxIter; 
	int nFold = cPar.nFold; 
	int separinds = cPar.separinds; 
	int useSummary = cPar.useSummary;

	// Estimate all parameters
	PrintCurrentTime(cPar); // Check Current Time
	CPRSppProc(Ys, Xs, A, cPar.lambda1, cPar.lambda2, b_est, b_est_2, b_re_est, b_sec, cPar.nzero, cPar);
	PrintCurrentTime(cPar); // Check Current Time

	std::cout << "Coordinate decent algorithm process [" << 0 << "] has been finished..." << std::endl;
	
	// Use n fold CV for selecting the tuning parameter lambda1 and lambda2 /////////////////////////////
	std::cout << "Conduct CV..." << std::endl;
	
	// Set seed number
	//srand(100);

	int i, j, k, cnt1, cnt2;
	int L1 = 0, L2 = cPar.lambda2.n_elem;
	arma::field<arma::fvec> _lambda1(L2);
	arma::fvec _lambda2;
	arma::fvec f_ind(Ys(0).n_elem);
	arma::fvec f_ind_(Ys(0).n_elem);
	arma::fvec nf = arma::zeros<arma::fvec>(nFold);
	arma::field<arma::fmat> mse(nFold);
	arma::field<arma::fvec> Ys_train(Ys.n_elem);
	arma::field<arma::fmat> Xs_train(Xs.n_elem);
	arma::fvec Ys_test;
	arma::fmat Xs_test;
	arma::field<arma::fmat> _b_est;
	arma::field<arma::fmat> _b_est_2, _b_re_est;
	arma::umat _nzero;

	// Compute max length of lambda1 and lambda2
	for (i=0; i<L2; i++) if (cPar.lambda1(i).n_elem > L1) L1 = cPar.lambda1(i).n_elem;
	std::cout << "Maximum length of lambda1 and lambda2: " << L1 << ", " << L2 << std::endl;

	// Set CV index
	for (i=0; i<Ys(0).n_elem; i++) { f_ind_(i) = i % nFold; nf(f_ind_(i))++; }
	f_ind = arma::shuffle(arma::shuffle(f_ind_));
	
	//for (i=0; i<Ys(0).n_elem; i++) std::cout << f_ind_(i) << " "; std::cout << std::endl;
	//for (i=0; i<Ys(0).n_elem; i++) std::cout << f_ind(i) << " "; std::cout << std::endl;
	//for (i=0; i<nFold; i++) std::cout << nf(i) << std::endl;

	// Initialize mse, Ys_train, Xs_train
	for (i=0; i<nFold; i++) {mse(i).set_size(L1, L2); mse(i).fill(arma::datum::inf);}
	for (i=1; i<Ys.n_elem; i++) Ys_train(i) = Ys(i);
	for (i=1; i<Xs.n_elem; i++) Xs_train(i) = Xs(i);

	// Conduct CV procedure
	for (i=0; i<nFold; i++) {

		// Set lambda1 and lambda2
		_lambda2 = cPar.lambda2;
		for (j=0; j<L2; j++) _lambda1(j) = cPar.lambda1(j);

		// Set Ys_train, Ys_test
		Ys_train(0) = arma::zeros<arma::fvec>(Ys(0).n_elem-nf(i));
		if (!separinds) for (k=1; k<Ys.n_elem; k++) Ys_train(k) = arma::zeros<arma::fvec>(Ys(k).n_elem-nf(i));////
		Ys_test = arma::zeros<arma::fvec>(nf(i));

		cnt1 = cnt2 = 0;
		for (j=0; j<Ys(0).n_elem; j++) {
			if (i == f_ind(j)) {Ys_test(cnt1) = Ys(0)(j); cnt1++;}
			else {Ys_train(0)(cnt2) = Ys(0)(j); if (!separinds) for (k=1; k<Ys.n_elem; k++) Ys_train(k)(cnt2) = Ys(k)(j); cnt2++;}/////
		}

		// Set Xs_train, Xs_test
		Xs_train(0) = arma::zeros<arma::fmat>(Xs(0).n_rows-nf(i),Xs(0).n_cols);
		if (!separinds) for (k=1; k<Xs.n_elem; k++) Xs_train(k) = arma::zeros<arma::fmat>(Xs(k).n_rows-nf(i),Xs(k).n_cols);/////
		Xs_test = arma::ones<arma::fmat>(nf(i), Xs(0).n_cols+1);

		cnt1 = cnt2 = 0;
		for (j=0; j<Xs(0).n_rows; j++) {
			if (i == f_ind(j)) {Xs_test(cnt1, arma::span(1,Xs(0).n_cols)) = Xs(0).row(j); cnt1++;}
			else {Xs_train(0).row(cnt2) =  Xs(0).row(j); if (!separinds) for (k=1; k<Xs.n_elem; k++) Xs_train(k).row(cnt2) =  Xs(k).row(j); cnt2++;}/////
		}
		
		// Conduct CV
		PrintCurrentTime(cPar); // Check Current Time
		CPRSppProc(Ys_train, Xs_train, A, _lambda1, _lambda2, _b_est, _b_est_2, _b_re_est, b_sec, _nzero, cPar);
		PrintCurrentTime(cPar); // Check Current Time
 
		std::cout << "Coordinate decent algorithm process [" << i+1 << "] has been finished..." << std::endl;

		// Compute MSE
		arma::fmat cvR2(L1,L2); cvR2.zeros(); //////
		CPRSppMSE(mse(i), _b_est, _lambda1, _lambda2, L1, L2, Ys_test, Xs_test, cvR2);
		//cvR2.save("./res/cvR2_" + cPar.fnameYs + "_" + cPar.penalize + "_" + patch::to_string(i+1) + ".txt", arma::csv_ascii); ///// 

	}

	// Evaluate the result of CV
	ComputeCVRes(b_min, b_min0, mse, b_est, cPar.lambda1, cPar.lambda2, nFold, cPar.nzero, cprRes, cvm);

	// Save Coefficients
	std::cout << "Save all coefficients..." << std::endl;
	b_min.save(cPar.output+".beta", arma::csv_ascii);
	b_min0.save(cPar.output+".beta0", arma::csv_ascii);
	PrintCurrentTime(cPar); // Check Current Time
	///////////////////////////////////////////////////////////////////////////////////////////////////

}

// Compute MSE and Prediction R2
void CPRSppPred(arma::fvec & Ys_test, arma::fmat & Xs_test, arma::fvec & b_min, arma::fvec & b_min0, arma::fvec & cprRes, PARAM & cPar){
	
	std::cout << "-------------------------------------------------------------" << std::endl;
	std::cout << "                  PREDICTION R2 AND MSE                      " << std::endl;
	std::cout << "-------------------------------------------------------------" << std::endl;
	std::cout << std::endl;

	float mse, R2, R2_1, mse0, R20, R20_1, slope, slope0;
	arma::fmat one = arma::ones<arma::fmat>(Xs_test.n_rows,1); 
	arma::fmat _Xs_test = Xs_test; _Xs_test.insert_cols(0, one);

	//std::cout << "_Xs_test "<< _Xs_test(0,0) << " " << _Xs_test(0,1) << " " << _Xs_test(0,2) << std::endl;
	//std::cout << "_Xs_test "<< _Xs_test(1,0) << " " << _Xs_test(1,1) << " " << _Xs_test(1,2) << std::endl;
	//std::cout << "Ys_test "<< Ys_test(0) << " " << Ys_test(1) << " " << Ys_test(2) << std::endl;

	arma::fvec Xsb = _Xs_test * b_min;
	arma::fvec Xsb0 = _Xs_test * b_min0;

	//std::cout << "Y_Xsb "<< Y_Xsb(0) << " " << Y_Xsb(1) << " " << Y_Xsb(2) << std::endl;
	//std::cout << "Y_Xsb0 "<< Y_Xsb0(0) << " " << Y_Xsb0(1) << " " << Y_Xsb0(2) << std::endl;

	mse = arma::mean(arma::square(Ys_test - Xsb));
	mse0 = arma::mean(arma::square(Ys_test - Xsb0));
	R2 = pow(arma::as_scalar(arma::cor(Ys_test, Xsb)),2);
	R20 = pow(arma::as_scalar(arma::cor(Ys_test, Xsb0)),2);
	R2_1 = 1.0-mse/arma::mean(arma::square(Ys_test-mean(Ys_test)));
	R20_1 = 1.0-mse0/arma::mean(arma::square(Ys_test-mean(Ys_test)));
	slope = arma::stddev(Ys_test)/arma::stddev(Xsb)*arma::as_scalar(arma::cor(Ys_test, Xsb));
	slope0 = arma::stddev(Ys_test)/arma::stddev(Xsb0)*arma::as_scalar(arma::cor(Ys_test, Xsb0));

	// Save Results
	cprRes(0)=R20; cprRes(1)=R20_1; cprRes(2)=mse0;
	cprRes(6)=R2; cprRes(7)=R2_1; cprRes(8)=mse;
	cprRes(13)=slope0; cprRes(14)=slope;
	cprRes.save(cPar.output+".res", arma::csv_ascii);

	// Print Results
	std::cout << "(1) Prediction Results with " << cPar.penalize << std::endl;
	std::cout << "lambda2=0" << ",lambda1=" << cprRes(3) << ",R2=" << R20 << ",R2(by MSE)=" << R20_1 << ",MSE=" << mse0;
	std::cout << ",Slope=" << slope0 << ",Nzbeta=" << cprRes(4) << ",cvMSE=" << cprRes(5) << std::endl;
	std::cout << std::endl;
	std::cout << "(2) Prediction Results  with " << cPar.penalize << " + " << cPar.penalize2 << std::endl;
	std::cout << "lambda2=" << cprRes(10) << ",lambda1=" << cprRes(9) << ",R2=" << R2 << ",R2(by MSE)=" << R2_1 << ",MSE=" << mse;
	std::cout << ",Slope=" << slope <<",Nzbeta=" << cprRes(11) << ",cvMSE=" << cprRes(12) << std::endl;
	std::cout << std::endl;
	std::cout << "End the CTPR Program!!!!!" << std::endl;
	PrintCurrentTime(cPar); // Check Current Time
}


// Main Function
int main(int argc, char **argv) {

	// Check whether paraemters are acceptable /////////
	if (argc <= 1) {
		PrintVersion();
		return EXIT_SUCCESS;
	}
	if (argc==2 && argv[1][0] == '-' && argv[1][1] == 'l') {
		PrintLicense();
		return EXIT_SUCCESS;
	}
	if (argc==2 && argv[1][0] == '-' && argv[1][1] == 'h') {
		PrintHelp(0);
		return EXIT_SUCCESS;
	}
	if (argc==3 && argv[1][0] == '-' && argv[1][1] == 'h') {
		std::string str;
		str.assign(argv[2]);
		PrintHelp(atoi(str.c_str()));
		return EXIT_SUCCESS;
	}
	////////////////////////////////////////////////////

	// Print CTPR Version and License //////////////////
	PrintVersion();
	PrintLicense();
	////////////////////////////////////////////////////

	// Initialize parameters ///////////////////////////
	PARAM cPar;
	cPar.useMPI = 0; // do not use MPI
	cPar.fnameXs = ""; cPar.fextXs = ""; cPar.fnameXstest = ""; cPar.fextXstest = "";
	cPar.fnameSs = ""; cPar.fextSs = "";
	cPar.fnameYs = ""; cPar.fnameYstest = "";
	cPar.ncolXs = 0; cPar.ncolXstest = 0;
	cPar.ncolYs = 0; cPar.ncolYstest = 0;
	cPar.useSummary = 0; cPar.useTest = 0; cPar.nsecTrait = 0; cPar.useScaling = 0;
	cPar.nrowXs.set_size(2); cPar.nrowXs(0)=0; 
	cPar.nrowYs.set_size(2); cPar.nrowYs(0)=0;
	cPar.nrowXstest = 0; cPar.nrowYstest = 0;
	cPar.penalize = "Lasso"; cPar.penalize2 = "CTPR";
	cPar.separinds = 0; cPar.nFold = 5;
	cPar.perc = 0.25;
	cPar.error = FALSE;	cPar.re_est = FALSE; cPar.slambda2 = FALSE;
	cPar.maxNz = 0; cPar.maxIter = 10000; 
	cPar.gamma = 3.0; cPar.tol = 0.0001; 
	cPar.output = "CTPRResult";
	cPar.nGroup = 0; cPar.start = 1; cPar.flamb1 = 0; cPar.llamb1 = 100;
	cPar.rank = 0; cPar.size = 0; cPar.ranktxt = "";
	cPar.t_start = clock(); // set current time
	////////////////////////////////////////////////////
	
	// Assign particular values to the paramters ///////
	if (!AssignParameters(argc, argv, cPar)) return EXIT_FAILURE;
	if (!cPar.slambda2) {
		cPar.lambda2.set_size(12);
		cPar.lambda2(0)=0; cPar.lambda2(1)=0.06109; cPar.lambda2(2)=0.13920 ; cPar.lambda2(3)=0.24257; cPar.lambda2(4)=0.38582;
		cPar.lambda2(5)=0.59756; cPar.lambda2(6)=0.94230; cPar.lambda2(7)=1.60280; cPar.lambda2(8)=3.37931; cPar.lambda2(9)=8.5;  
		cPar.lambda2(10)=15.5; cPar.lambda2(11)=24.5;
	} 
	////////////////////////////////////////////////////
	
	// Check All datasets //////////////////////////////
	PrintCurrentTime(cPar); // Check Current Time
	if (!CheckData(cPar.fnameXs, cPar.fnameXstest, cPar.fnameYs, cPar.fnameYstest, cPar.fnameSs, cPar)) return EXIT_FAILURE;	
	PrintCurrentTime(cPar); // Check Current Time
	////////////////////////////////////////////////////

	// Check Parameter combination ////////////////////
	if (!CheckCombination(cPar)) return EXIT_FAILURE;
	///////////////////////////////////////////////////

	// Set A matrix ////////////////////////////////////
	int i, j, k;
	arma::fcube A; A.set_size(cPar.ncolYs+cPar.nsecTrait, cPar.ncolYs+cPar.nsecTrait, cPar.ncolXs);
	arma::fmat A2(cPar.ncolYs+cPar.nsecTrait,cPar.ncolYs+cPar.nsecTrait); A2.fill(1); A2.diag()-=1;
	for (i=0; i<cPar.ncolXs; i++) A.slice(i) = A2;
	////////////////////////////////////////////////////

	// Read Phenotype, Genotype and Summary files //////
  arma::field<arma::fvec> Ys(cPar.ncolYs);
	arma::field<arma::fmat> Xs(cPar.ncolYs);
	arma::fvec Ystest, imr2;
	arma::fmat Xstest, b_sec, seb_sec; 
	float avgseb; 

	PrintCurrentTime(cPar); // Check Current Time
	LoadPheno(Ys, Ystest, cPar.fnameYs, cPar.fnameYstest, cPar); // Read Phenotype File
	LoadGeno(Xs, Xstest, cPar.fnameXs, cPar.fnameXstest, cPar); // Read Genotype File
	if (cPar.useSummary) {
		LoadSummary(b_sec, seb_sec, imr2, cPar.fnameSs, cPar); // Read Summary File
		// reset A matrix
		avgseb = arma::mean(arma::mean(seb_sec));
		arma::fmat A3(cPar.ncolYs+cPar.nsecTrait,cPar.ncolYs+cPar.nsecTrait); A3.fill(1); A3.diag()-=1;
		for (i=0; i<cPar.ncolXs; i++) {
			for (j=0; j<cPar.ncolYs; j++) {
				for (k=j+1; k<cPar.ncolYs; k++) {A3(j,k) = A3(k,j) = 1; /*imr2(i)/avgseb;*/}
				for (k=cPar.ncolYs; k<cPar.ncolYs+cPar.nsecTrait; k++) {A3(j,k) = A3(k,j) = avgseb/seb_sec(k-cPar.ncolYs,i); /*imr2(i)/seb_sec(k-ncolYs,i);*/}
			}
			//A.slice(i) = A3; // Keep the default A matrix
		}
	}
	PrintCurrentTime(cPar); // Check Current Time
	////////////////////////////////////////////////////

	// Scaling for secondary traits ////////////////////
	if (cPar.useScaling) { 
		if (ScaleTraits(Ys, Xs, cPar.fnameYs, cPar)) return EXIT_SUCCESS;
		else return EXIT_FAILURE;
	}
	////////////////////////////////////////////////////

	// Estimate parameters and select lambda1, labmda2 and Save beta coefficients
	arma::field<arma::fmat> b_est, b_est_2, b_re_est;
	arma::fvec bmin, bmin0;
	arma::fmat cvm;
	arma::fvec cprRes; cprRes.set_size(15); // result file
	CPRSppTune(Ys, Xs, A,	b_est, b_est_2, b_re_est, b_sec, bmin, bmin0, cvm, cprRes, cPar); 
	//////////////////////////////////////////////////////

	// Compute Prediction R2 and Save prediction results 
	if (cPar.useTest) CPRSppPred(Ystest, Xstest, bmin, bmin0, cprRes, cPar); 
	//////////////////////////////////////////////////////

	return EXIT_SUCCESS;
}
