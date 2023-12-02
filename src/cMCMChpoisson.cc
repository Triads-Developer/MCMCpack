////////////////////////////////////////////////////////////////////
// cMCMChpoisson.cc
//
// cMCMChpoisson samples from the posterior distribution of a
// Poisson hierarchical linear regression model with a log link function
//
// The code uses Algorithm 2 of Chib & Carlin (1999) for efficient
// inference of (\beta | Y, sigma^2, Vb).
//
// Chib, S. & Carlin, B. P. (1999) On MCMC sampling in hierarchical
// longitudinal models, Statistics and Computing, 9, 17-26
//
////////////////////////////////////////////////////////////////////
//
// Original code by Ghislain Vieilledent, may 2011
// CIRAD UR B&SEF
// ghislain.vieilledent@cirad.fr / ghislainv@gmail.com
//
////////////////////////////////////////////////////////////////////
//
// The initial version of this file was generated by the
// auto.Scythe.call() function in the MCMCpack R package
// written by:
//
// Andrew D. Martin
// Dept. of Political Science
// Washington University in St. Louis
// admartin@wustl.edu
//
// Kevin M. Quinn
// Dept. of Government
// Harvard University
// kevin_quinn@harvard.edu
//
// This software is distributed under the terms of the GNU GENERAL
// PUBLIC LICENSE Version 2, June 1991.  See the package LICENSE
// file for more information.
//
// Copyright (C) 2011 Andrew D. Martin and Kevin M. Quinn
//
////////////////////////////////////////////////////////////////////
//
// Revisions:
// - This file was initially generated on Wed May  4 10:42:50 2011
// - G. Vieilledent, on May 9 2011
//
////////////////////////////////////////////////////////////////////

#include "matrix.h"
#include "distributions.h"
#include "stat.h"
#include "la.h"
#include "ide.h"
#include "smath.h"
#include "MCMCrng.h"
#include "MCMCfcds.h"

#include <R.h>           // needed to use Rprintf()
#include <R_ext/Utils.h> // needed to allow user interrupts

using namespace scythe;
using namespace std;

extern "C"{

    /* Gibbs sampler function */
    void cMCMChpoisson (

	// Constants and data
	const int *ngibbs, const int *nthin, const int *nburn, // Number of iterations, burning and samples
	const int *nobs, const int *ngroup, // Constants
	const int *np, const int *nq, // Number of fixed and random covariates
	const int *IdentGroup, // Vector of group
	const double *Y_vect, // Observed response variable
	const double *X_vect, // Covariate for fixed effects
	const double *W_vect, // Covariate for random effects
        // Parameters to save
	double *beta_vect, // Fixed effects
	double *b_vect, // Random effects
	double *Vb_vect, // Variance of random effects
	double *V, // Variance of residuals
	// Defining priors
	const double *mubeta_vect, const double *Vbeta_vect,
	const double *r, const double *R_vect,
	const double *s1_V, const double *s2_V,
	// Diagnostic
	double *Deviance,
	double *lambda_pred, // Annual mortality rate
	// Seeds
	const int *seed,
	// Verbose
	const int *verbose,
	// Overdispersion
	const int *FixOD

	) {

	////////////////////////////////////////////////////////////////////////////////
	//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
	// Defining and initializing objects

        ///////////////////////////
	// Redefining constants //
	const int NGIBBS=ngibbs[0];
	const int NTHIN=nthin[0];
	const int NBURN=nburn[0];
	const int NSAMP=(NGIBBS-NBURN)/NTHIN;
	const int NOBS=nobs[0];
	const int NGROUP=ngroup[0];
	const int NP=np[0];
	const int NQ=nq[0];

	///////////////
	// Constants //
	// Number of observations by group k
	int *nobsk = new int[NGROUP];
	for (int k=0; k<NGROUP; k++) {
	    nobsk[k]=0;
	    for (int n=0; n<NOBS; n++) {
		if (IdentGroup[n]==k) {
		    nobsk[k]+=1;
		}
	    }
	}
	// Position of each group in the data-set
	int **posk_arr = new int*[NGROUP];
	for (int k=0; k<NGROUP; k++) {
	    posk_arr[k] = new int[nobsk[k]];
	    int repk=0;
	    for (int n=0; n<NOBS; n++) {
	    	if (IdentGroup[n]==k) {
	    	    posk_arr[k][repk]=n;
	    	    repk++;
	    	}
	    }
	}
	// Small fixed matrices indexed on k for data access
	Matrix<double> *Xk_arr = new Matrix<double>[NGROUP];
        Matrix<double> *Wk_arr = new Matrix<double>[NGROUP];
	for(int k=0; k<NGROUP; k++) {
	    Xk_arr[k] = Matrix<double>(nobsk[k],NP);
	    Wk_arr[k] = Matrix<double>(nobsk[k],NQ);
	    for (int m=0; m<nobsk[k]; m++) {
		for (int p=0; p<NP; p++) {
		    Xk_arr[k](m,p)=X_vect[p*NOBS+posk_arr[k][m]];
		}
		for (int q=0; q<NQ; q++) {
		    Wk_arr[k](m,q)=W_vect[q*NOBS+posk_arr[k][m]];
		}
	    }
	}
	Matrix<double> *tXk_arr = new Matrix<double>[NGROUP];
	Matrix<double> *tWk_arr = new Matrix<double>[NGROUP];
	Matrix<double> *cpXk_arr = new Matrix<double>[NGROUP];
	Matrix<double> *tXWk_arr = new Matrix<double>[NGROUP];
	Matrix<double> *tWXk_arr = new Matrix<double>[NGROUP];
	for(int k=0; k<NGROUP; k++) {
	    tXk_arr[k] = t(Xk_arr[k]);
	    tWk_arr[k] = t(Wk_arr[k]);
	    cpXk_arr[k] = crossprod(Xk_arr[k]);
	    tXWk_arr[k] = t(Xk_arr[k])*Wk_arr[k];
	    tWXk_arr[k] = t(Wk_arr[k])*Xk_arr[k];
	}

	////////////////////
	// Priors objects //
	Matrix<double> mubeta(NP,1,mubeta_vect);
	Matrix<double> Vbeta(NP,NP,Vbeta_vect);
	Matrix<double> R(NQ,NQ,R_vect);

	/////////////////////////////////////
	// Initializing running parameters //
	Matrix<double> *bk_run = new Matrix<double>[NGROUP]; // Random effects
	for (int k=0;k<NGROUP;k++) {
	    bk_run[k] = Matrix<double>(NQ,1);
	    for (int q=0; q<NQ; q++) {
		bk_run[k](q)=b_vect[q*NGROUP*NSAMP+k*NSAMP];
	    }
	}
	Matrix<double> beta_run(NP,1,false); // Unicolumn matrix of fixed effects
	for (int p=0; p<NP; p++) {
	    beta_run(p)=beta_vect[p*NSAMP];
	}
	Matrix<double> Vb_run(NQ,NQ,true,0.0);
	for (int q=0; q<NQ; q++) {
	    for (int qprim=0; qprim<NQ; qprim++) {
		Vb_run(q,qprim)=Vb_vect[qprim*NQ*NSAMP+q*NSAMP];
	    }
	}
	double V_run=V[0];
	Matrix<double> *log_lambdak_run = new Matrix<double>[NGROUP];
	for (int k=0;k<NGROUP;k++) {
	    log_lambdak_run[k] = Matrix<double>(nobsk[k],1);
	    for (int m=0; m<nobsk[k]; m++) {
		log_lambdak_run[k](m)=log(lambda_pred[posk_arr[k][m]]);
		lambda_pred[posk_arr[k][m]]=0; // We reinitialize lambda_pred to zero to compute the posterior mean
	    }
	}
	double Deviance_run=Deviance[0];

        ////////////////////////////////////////////////////////////
	// Proposal variance and acceptance for adaptive sampling //
	double *sigmap = new double[NOBS];
	double *nA = new double[NOBS];
	for (int n=0; n<NOBS; n++) {
	    nA[n]=0;
	    sigmap[n]=1;
	}
	double *Ar = new double[NOBS]; // Acceptance rate
	for (int n=0; n<NOBS; n++) {
	    Ar[n]=0.0;
	}

	////////////
	// Message//
	Rprintf("\nRunning the Gibbs sampler. It may be long, keep cool :)\n\n");
	R_FlushConsole();
	//R_ProcessEvents(); for windows

	/////////////////////
	// Set random seed //
	mersenne myrng;
	myrng.initialize(*seed);

	///////////////////////////////////////////////////////////////////////////////////////
	//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
	// Gibbs sampler (see Chib et Carlin, 1999 p.4 "Blocking for Gaussian mixed models")

	for (int g=0;g<NGIBBS;g++) {

            ////////////////////////////////////////////////
            // vector log_lambda : Metropolis algorithm //

	    for (int k=0; k<NGROUP; k++) {

	    	for (int m=0; m<nobsk[k]; m++) {

	    	    // Which one ?
	    	    int w=posk_arr[k][m];

                    // Mean of the prior
	    	    Matrix<double> log_lambda_hat=Xk_arr[k](m,_)*beta_run+Wk_arr[k](m,_)*bk_run[k];

	    	    // Proposal
	    	    double log_lambda_prop=myrng.rnorm(log_lambdak_run[k](m),sigmap[w]);

	    	    // lambda
	    	    double lambda_prop=exp(log_lambda_prop);
	    	    double lambda_run=exp(log_lambdak_run[k](m));

	    	    // Ratio of probabilities
	    	    double p_prop=log(dpois(Y_vect[w],lambda_prop))+
	    		log(dnorm(log_lambda_prop,log_lambda_hat(0),sqrt(V_run)));

	    	    double p_now=log(dpois(Y_vect[w],lambda_run))+
	    		log(dnorm(log_lambdak_run[k](m),log_lambda_hat(0),sqrt(V_run)));

	    	    double r=exp(p_prop-p_now); // ratio
	    	    double z=myrng.runif();

	    	    // Actualization
	    	    if (z < r) {
	    		log_lambdak_run[k](m)=log_lambda_prop;
	    		nA[w]++;
	    	    }
	    	}
	    }


	    //////////////////////////////////
	    // vector beta: Gibbs algorithm //

	    // invVi, sum_V and sum_v
	    Matrix<double> sum_V(NP,NP);
	    Matrix<double> sum_v(NP,1);
	    for (int k=0; k<NGROUP; k++) {
	    	sum_V += (1/V_run)*cpXk_arr[k]-pow(1/V_run,2)*tXWk_arr[k]*invpd(invpd(Vb_run)+tWk_arr[k]*(1/V_run)*Wk_arr[k])*tWXk_arr[k];
	    	sum_v += (1/V_run)*tXk_arr[k]*log_lambdak_run[k]-pow(1/V_run,2)*tXWk_arr[k]*invpd(invpd(Vb_run)+tWk_arr[k]*(1/V_run)*Wk_arr[k])*tWk_arr[k]*log_lambdak_run[k];
	    }

	    // big_V
	    Matrix<double> big_V=invpd(invpd(Vbeta)+sum_V/V_run);

	    // small_v
	    Matrix<double> small_v=invpd(Vbeta)*mubeta+sum_v/V_run;

	    // Draw in the posterior distribution
	    beta_run=myrng.rmvnorm(big_V*small_v,big_V);


            ///////////////////////////////
	    // vector b: Gibbs algorithm //

	    // Loop on group
	    for (int k=0; k<NGROUP; k++) {

	    	// big_Vk
	    	Matrix<double> big_Vk=invpd(invpd(Vb_run)+crossprod(Wk_arr[k])/V_run);

	    	// small_vk
	    	Matrix<double> small_vk=(t(Wk_arr[k])*(log_lambdak_run[k]-Xk_arr[k]*beta_run))/V_run;

	    	// Draw in the posterior distribution
	    	bk_run[k]=myrng.rmvnorm(big_Vk*small_vk,big_Vk);
	    }


	    ////////////////////////////////////////////
	    // vector of variance Vb: Gibbs algorithm //
	    Matrix<double> SSBb(NQ,NQ,true,0.0);
	    for(int k=0; k<NGROUP; k++) {
	    	SSBb+=bk_run[k]*t(bk_run[k]);
	    }
	    int Vb_dof=(*r)+(NGROUP);
	    Matrix<double> Vb_scale = invpd(SSBb+(*r)*R);
	    Vb_run=invpd(myrng.rwish(Vb_dof,Vb_scale));


	    ////////////////
	    // variance V //

	    // e
	    Matrix<double> e(1,1,true,0.0);
	    for (int k=0; k<NGROUP; k++) {
	    	e+=crossprod(log_lambdak_run[k]-Xk_arr[k]*beta_run-Wk_arr[k]*bk_run[k]);
	    }

	    // Parameters
	    double S1=*s1_V+(NOBS/2); //shape
	    double S2=*s2_V+0.5*e(0); //rate

	    // Draw in the posterior distribution
	    if (FixOD[0]==1) {
	    	V_run=V[0];
	    }
	    else {
	    	V_run=1/myrng.rgamma(S1,S2);
	    }


	    //////////////////////////////////////////////////
	    //// Deviance

	    // logLikelihood
	    double logLk=0;
	    for (int k=0; k<NGROUP; k++) {
	    	for (int m=0; m<nobsk[k]; m++) {
	    	    // Which one ?
	    	    int w=posk_arr[k][m];
	    	    // lambda_run
	    	    double lambda_run=exp(log_lambdak_run[k](m));
	    	    // L
	    	    logLk+=log(dpois(Y_vect[w],lambda_run));
	    	}
	    }

	    // Deviance
	    Deviance_run=-2*logLk;


	    //////////////////////////////////////////////////
	    // Output
	    if(((g+1)>NBURN) && (((g+1)%(NTHIN))==0)){
	    	int isamp=((g+1)-NBURN)/(NTHIN);
	    	for (int p=0; p<NP; p++) {
	    	    beta_vect[p*NSAMP+(isamp-1)]=beta_run(p);
	    	}
	    	for (int k=0; k<NGROUP; k++) {
	    	    for (int q=0; q<NQ; q++) {
	    		b_vect[q*NGROUP*NSAMP+k*NSAMP+(isamp-1)]=bk_run[k](q);
	    	    }
	    	}
	    	for (int q=0; q<NQ; q++) {
	    	    for (int qprim=0; qprim<NQ; qprim++) {
	    		Vb_vect[qprim*NQ*NSAMP+q*NSAMP+(isamp-1)]=Vb_run(q,qprim);
	    	    }
	    	}
	    	V[isamp-1]=V_run;
	    	Deviance[isamp-1]=Deviance_run;
	    	for (int k=0;k<NGROUP;k++){
	    	    for (int m=0; m<nobsk[k]; m++) {
	    		int w=posk_arr[k][m];
			Matrix<double> log_lambda_hat=Xk_arr[k](m,_)*beta_run+Wk_arr[k](m,_)*bk_run[k];
	    		lambda_pred[w]+=exp(log_lambda_hat(0)+0.5*V_run)/(NSAMP); // We compute the mean of NSAMP values
	    	    }
	    	}
	    }


	    ///////////////////////////////////////////////////////
	    // Adaptive sampling (on the burnin period)
	    int DIV=0;
	    if (NGIBBS >=1000) DIV=100;
	    else DIV=NGIBBS/10;
	    if((g+1)%DIV==0 && (g+1)<=NBURN){
	    	for (int n=0; n<NOBS; n++) {
		    const double ropt=0.44;
	    	    Ar[n]=nA[n]/(DIV*1.0);
	    	    if(Ar[n]>=ropt) sigmap[n]=sigmap[n]*(2-(1-Ar[n])/(1-ropt));
	    	    else sigmap[n]=sigmap[n]/(2-Ar[n]/ropt);
	    	    nA[n]=0.0; // We reinitialize the number of acceptance to zero
	    	}
	    }
	    if((g+1)%DIV==0 && (g+1)>NBURN){
	    	for (int n=0; n<NOBS; n++) {
	    	    Ar[n]=nA[n]/(DIV*1.0);
	    	    nA[n]=0.0; // We reinitialize the number of acceptance to zero
	    	}
	    }


	    //////////////////////////////////////////////////
	    // Progress bar
	    double Perc=100*(g+1)/(NGIBBS);
	    if(((g+1)%(NGIBBS/100))==0 && (*verbose==1)){
	    	Rprintf("*");
	    	R_FlushConsole();
	    	//R_ProcessEvents(); for windows
	    	if(((g+1)%(NGIBBS/10))==0){
		    double mAr=0; // Mean acceptance rate
		    for (int n=0; n<NOBS; n++) {
			mAr+=Ar[n]/(NOBS);
		    }
		    Rprintf(":%.1f%%, mean accept. rate=%.3f\n",Perc,mAr);
      	    	    R_FlushConsole();
	    	    //R_ProcessEvents(); for windows
	    	}
	    }


            //////////////////////////////////////////////////
	    // User interrupt
	    R_CheckUserInterrupt(); // allow user interrupts

	} // end MCMC loop

	///////////////
	// Delete memory allocation
	delete[] nobsk;
	for(int k=0; k<NGROUP; k++) {
	    delete[] posk_arr[k];
	}
	delete[] sigmap;
	delete[] nA;
	delete[] posk_arr;
	delete[] Xk_arr;
	delete[] Wk_arr;
	delete[] tXk_arr;
	delete[] tWk_arr;
	delete[] cpXk_arr;
	delete[] tXWk_arr;
	delete[] tWXk_arr;
	delete[] bk_run;
	delete[] log_lambdak_run;
	delete[] Ar;

    } // end cMCMChpoisson

} // end extern "C"

////////////////////////////////////////////////////////////////////
// END
////////////////////////////////////////////////////////////////////
