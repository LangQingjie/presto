#include "presto.h"

/* Number of bins on each side of a freq to use for interpolation */
#define INTERPBINS 5

static char num[41][5] =
{"0th", "1st", "2nd", "3rd", "4th", "5th", "6th", \
 "7th", "8th", "9th", "10th", "11th", "12th", \
 "13th", "14th", "15th", "16th", "17th", "18th", \
 "19th", "20th", "21st", "22nd", "23rd", "24th", \
 "25th", "26th", "27th", "28th", "29th", "30th", \
 "31st", "32nd", "33rd", "34th", "35th", "36th", \
 "37th", "38th", "39th", "40th"};

/* Routines defined at the bottom */

static int padfftlen(int minifftlen, int numbetween, int *padlen);
float percolate_rawbincands(rawbincand *cands, int numcands);
float percolate_fftcands(fftcand *cands, int numcands);
void print_rawbincand(rawbincand cand);


fftcand *search_fft(fcomplex *fft, int numfft, int lobin, int numharmsum,
		    int numbetween, presto_interptype interptype,
		    float norm, float sigmacutoff, int *numcands, 
		    float *powavg, float *powvar, float *powmax)
/* This routine searches a short FFT of 'numfft' complex freqs      */
/* and returns a candidate vector of fftcand structures containing  */
/* information about the best candidates found.                     */
/* The routine uses either interbinning or interpolation as well    */
/* as harmonic summing during the search.                           */
/* The number of candidates returned is either 'numcands' if != 0,  */
/* or is determined automatically by 'sigmacutoff' -- which         */
/* takes into account the number of bins searched.                  */
/* The returned vector is sorted in order of decreasing power.      */
/* Arguments:                                                       */
/*   'fft' is the FFT to search (complex valued)                    */
/*   'numfft' is the number of complex points in 'fft'              */
/*   'lobin' is the lowest Fourier freq to search                   */
/*   'numharmsum' the number of harmonics to sum during the search  */
/*   'numbetween' the points to interpolate per bin                 */
/*   'interptype' is either INTERBIN or INTERPOLATE.                */
/*      INTERBIN = (interbinning) is fast but less sensitive.       */
/*      INTERPOLATE = (Fourier interpolation) is slower but more    */
/*        sensitive.                                                */
/*   'norm' is the normalization constant to multiply each power by */
/*   'sigmacutoff' if the number of candidates will be determined   */
/*      automatically, is the minimum Gaussian significance of      */
/*      candidates to keep -- taking into account the number of     */
/*      bins searched                                               */
/*   'numcands' if !0, is the number of candates to return.         */
/*      if 0, is a return value giving the number of candidates.    */
/*   'powavg' is a return value giving the average power level      */
/*   'powvar' is a return value giving the power level variance     */
/*   'powmax' is a return value giving the maximum power            */
{
  int ii, jj, offset, numtosearch, dynamic=0;
  int numspread=0, kern_half_width, numkern=0, nc=0, startnc=10;
  float powargr, powargi, *fullpows=NULL, *sumpows, ftmp;
  double twobypi, minpow=0.0, tmpminpow, dr, davg, dvar;
  static int firsttime=1, old_numfft=0;
  static fcomplex *kernel;
  fftcand *cands;
  fcomplex *spread, *kern;

  /* Override the value of numbetween if interbinning */

  if (interptype == INTERBIN)
    numbetween = 2;
  lobin = lobin * numbetween;
  norm = 1.0 / norm;

  /* Decide if we will manage the number of candidates */

  if (*numcands > 0)
    startnc = *numcands;
  else {
    dynamic = 1;
    minpow = power_for_sigma(sigmacutoff, 1, numfft-lobin);
  }
  cands = (fftcand *)malloc(startnc * sizeof(fftcand));
  for (ii=0; ii<startnc; ii++)
    cands[ii].p = 0.0;

  /* Prep some other values we will need */

  dr = 1.0 / (double) numbetween;
  twobypi = 1.0 / PIBYTWO;
  numtosearch = numfft * numbetween;
  numspread = padfftlen(numfft, numbetween, &kern_half_width);

  /* Prep the interpolation kernel if needed */

  if (interptype == INTERPOLATE){
    if (firsttime || (old_numfft != numfft)){
      if (!firsttime) free(kernel);
      numkern = 2 * numbetween * kern_half_width;
      kern = gen_r_response(0.0, numbetween, numkern);
      kernel = gen_cvect(numspread);
      place_complex_kernel(kern, numkern, kernel, numspread);
      COMPLEXFFT(kernel, numspread, -1);
      free(kern);
      firsttime = 0;
      old_numfft = numfft;
    }
  }
  
  /* Spread and interpolate the fft */
  
  spread = gen_cvect(numspread);
  spread_with_pad(fft, numfft, spread, numspread, numbetween, 0);
  /* Nyquist is in spread[0].i, but it is usually */
  /* _big_ so we won't use it.                    */
  spread[0].r = spread[numtosearch].r = 1.0;
  spread[0].i = spread[numtosearch].i = 0.0;
  if (interptype == INTERPOLATE){  /* INTERPOLATE */
    spread = complex_corr_conv(spread, kernel, numspread, \
			       FFTD, INPLACE_CORR);
  } else {                         /* INTERBIN */
    for (ii = 1; ii < numtosearch; ii += 2){
      spread[ii].r = twobypi * (spread[ii-1].r - spread[ii+1].r);
      spread[ii].i = twobypi * (spread[ii-1].i - spread[ii+1].i);
    }
  }
  fullpows = gen_fvect(numtosearch);

  /* First generate the original powers in order to         */
  /* calculate the statistics.  Yes, this is inefficient... */

  for (ii = lobin, jj = 0; ii < numfft; ii++, jj++){
    ftmp = POWER(fft[ii].r, fft[ii].i) * norm;
    fullpows[jj] = ftmp;
    if (ftmp > *powmax) *powmax = ftmp;
  }
  avg_var(fullpows, numfft-lobin, &davg, &dvar);
  *powavg = davg;
  *powvar = dvar;
  fullpows[0] = 1.0;
  for (ii = 1; ii < numtosearch; ii++)
    fullpows[ii] = POWER(spread[ii].r, spread[ii].i) * norm;
  free(spread);

  /* Search the raw powers */
  
  for (ii = lobin; ii < numtosearch; ii++) {
    if (fullpows[ii] > minpow){
      cands[startnc-1].r = dr * (double) ii;
      cands[startnc-1].p = fullpows[ii];
      tmpminpow = percolate_fftcands(cands, startnc);
      if (dynamic){
	if (nc==startnc){
	  startnc *= 2;
	  cands = (fftcand *)realloc(cands, startnc * sizeof(fftcand));
	  for (jj=nc; jj<startnc; jj++)
	    cands[jj].p = 0.0;
	}
	nc++;
      } else {
	minpow = tmpminpow;
	if (nc<startnc) nc++;
      }
    }
  }

  /* If needed, sum and search the harmonics */
  
  if (numharmsum > 1){
    sumpows = gen_fvect(numtosearch);
    memcpy(sumpows, fullpows, sizeof(float) * numtosearch);
    for (ii = 2; ii <= numharmsum; ii++){
      offset = ii / 2;
      if (dynamic)
	minpow = power_for_sigma(sigmacutoff, ii, numfft-lobin);
      else
	minpow = cands[startnc-1].p;
      for (jj = lobin; jj < numtosearch; jj++){
	sumpows[jj] += fullpows[(jj + offset) / ii];
	if (sumpows[jj] > minpow) {
	  cands[startnc-1].r = dr * (double) ii;
	  cands[startnc-1].p = sumpows[ii];
	  tmpminpow = percolate_fftcands(cands, startnc);
	  if (dynamic){
	    if (nc==startnc){
	      startnc *= 2;
	      cands = (fftcand *)realloc(cands, startnc * sizeof(fftcand));
	      for (jj=nc; jj<startnc; jj++)
		cands[jj].p = 0.0;
	    }
	    nc++;
	  } else {
	    minpow = tmpminpow;
	    if (nc<startnc) nc++;
	  }
	}
      }
    }
    free(sumpows);
  }
  free(fullpows);

  /* Add the rest of the fftcand data to the candidate array */

  for (ii=0; ii<startnc; ii++)
    cands[ii].nsum = numharmsum;

  /* Chop off the unused parts of the dynamic array */
  
  if (dynamic)
    cands = (fftcand *)realloc(cands, nc * sizeof(fftcand));
  *numcands = nc;
  return cands;
}


void search_minifft(fcomplex *minifft, int numminifft, \
		    rawbincand *cands, int numcands, int numharmsum, \
		    int numbetween, double numfullfft, double timefullfft, \
		    double lorfullfft, presto_interptype interptype, \
		    presto_checkaliased checkaliased)
  /* This routine searches a short FFT (usually produced using the   */
  /* MiniFFT binary search method) and returns a candidte vector     */
  /* containing information about the best binary candidates found.  */
  /* The routine uses either interbinning or interpolation as well   */
  /* as harmonic summing during the search.                          */
  /* Arguments:                                                      */
  /*   'minifft' is the FFT to search (complex valued)               */
  /*   'numminifft' is the number of complex points in 'minifft'     */
  /*   'cands' is a pre-allocated vector of rawbincand type in which */
  /*      the sorted (in decreasing sigma) candidates are returned   */
  /*   'numcands' is the length of the 'cands' vector                */
  /*   'numharmsum' the number of harmonics to sum during the search */
  /*   'numbetween' the points to interpolate per bin                */
  /*   'numfullfft' the number of points in the original long FFT    */
  /*   'timefullfft' the duration of the original time series (s)    */
  /*   'lorfullfft' the 1st bin of the long FFT that was miniFFT'd   */
  /*   'interptype' is either INTERBIN or INTERPOLATE.               */
  /*      INTERBIN = (interbinning) is fast but less sensitive.      */
  /*      INTERPOLATE = (Fourier interpolation) is slower but more   */
  /*        sensitive.                                               */
  /*   'checkaliased' is either CHECK_ALIASED or NO_CHECK_ALIASED.   */
  /*      NO_CHECK_ALIASED = harmonic summing does not include       */
  /*        aliased freqs making it faster but less sensitive.       */
  /*      CHECK_ALIASED = harmonic summing includes aliased freqs    */
  /*        making it slower but more sensitive.                     */
{
  int ii, jj, fftlen, offset, numtosearch;
  int numspread = 0, kern_half_width, numkern = 0;
  float powargr, powargi, *fullpows = NULL, *sumpows;
  double twobypi, minpow = 0.0, minsig, dr;
  static int firsttime = 1, old_numminifft = 0;
  static fcomplex *kernel;
  fcomplex *spread, *kern;

  /* Override the value of numbetween if interbinning */

  if (interptype == INTERBIN)
    numbetween = 2;

  /* Prep some other values we will need */

  dr = 1.0 / (double) numbetween;
  twobypi = 1.0 / PIBYTWO;
  fftlen = numminifft * numbetween;
  numspread = padfftlen(numminifft, numbetween, &kern_half_width);
  for (ii = 0; ii < numcands; ii++){
    cands[ii].mini_sigma = 0.0;
    cands[ii].mini_power = 0.0;
  }

  /* Prep the interpolation kernel if needed */

  if (interptype == INTERPOLATE){
    if (firsttime || (old_numminifft != numminifft)){
      if (!firsttime) free(kernel);
      numkern = 2 * numbetween * kern_half_width;
      kern = gen_r_response(0.0, numbetween, numkern);
      kernel = gen_cvect(numspread);
      place_complex_kernel(kern, numkern, kernel, numspread);
      COMPLEXFFT(kernel, numspread, -1);
      free(kern);
      firsttime = 0;
      old_numminifft = numminifft;
    }
  }
  
  /* Spread and interpolate the minifft */
  
  spread = gen_cvect(numspread);
  spread_with_pad(minifft, numminifft, spread, numspread, numbetween, 0);
  /* Nyquist is in spread[0].i, but it is usually */
  /* _big_ so we won't use it.                    */
  spread[0].r = spread[fftlen].r = 1.0;
  spread[0].i = spread[fftlen].i = 0.0;
  if (interptype == INTERPOLATE){  /* INTERPOLATE */
    spread = complex_corr_conv(spread, kernel, numspread, \
			       FFTD, INPLACE_CORR);
  } else {                         /* INTERBIN */
    for (ii = 1; ii < fftlen; ii += 2){
      spread[ii].r = twobypi * (spread[ii-1].r - spread[ii+1].r);
      spread[ii].i = twobypi * (spread[ii-1].i - spread[ii+1].i);
    }
  }

  numtosearch = (checkaliased == CHECK_ALIASED) ? 2 * fftlen : fftlen;
  fullpows = gen_fvect(numtosearch);
  fullpows[0] = 1.0;
  if (checkaliased == CHECK_ALIASED)
    fullpows[fftlen] = 1.0;  /* used to be nyquist^2 */

  /* The following wraps the data around the Nyquist freq such that */
  /* we consider aliased frequencies as well (If CHECK_ALIASED).    */
  
  if (checkaliased == CHECK_ALIASED)
    for (ii = 1, jj = numtosearch - 1; ii < fftlen; ii++, jj--)
      fullpows[ii] = fullpows[jj] = POWER(spread[ii].r, spread[ii].i);
  else
    for (ii = 1; ii < numtosearch; ii++)
      fullpows[ii] = POWER(spread[ii].r, spread[ii].i);
  free(spread);

  /* Search the raw powers */

  for (ii = 1; ii < numtosearch; ii++) {
    if (fullpows[ii] > minpow) {
      cands[numcands-1].mini_r = dr * (double) ii; 
      cands[numcands-1].mini_power = fullpows[ii];
      cands[numcands-1].mini_numsum = 1.0;
      cands[numcands-1].mini_sigma = 
	candidate_sigma(fullpows[ii], 1, 1);
      minsig = percolate_rawbincands(cands, numcands);
      minpow = cands[numcands-1].mini_power;
    }
  }

  /* If needed, sum and search the harmonics */
  
  if (numharmsum > 1){
    sumpows = gen_fvect(numtosearch);
    memcpy(sumpows, fullpows, sizeof(float) * numtosearch);
    for (ii = 2; ii <= numharmsum; ii++){
      offset = ii / 2;
      minpow = power_for_sigma(cands[numcands-1].mini_sigma, ii, 1);
      for (jj = 0; jj < numtosearch; jj++){
	sumpows[jj] += fullpows[(jj + offset) / ii];
	if (sumpows[jj] > minpow) {
	  cands[numcands-1].mini_r = dr * (double) jj; 
	  cands[numcands-1].mini_power = sumpows[jj];
	  cands[numcands-1].mini_numsum = (double) ii;
	  cands[numcands-1].mini_sigma = 
	    candidate_sigma(sumpows[jj], ii, 1);
	  minsig = percolate_rawbincands(cands, numcands);
	  minpow = 
	    power_for_sigma(cands[numcands-1].mini_sigma, ii, 1);
	}
      }
    }
    free(sumpows);
  }
  free(fullpows);

  /* Add the rest of the rawbincand data to the candidate array */

  for (ii = 0; ii < numcands; ii++){
    cands[ii].full_N = numfullfft;
    cands[ii].full_T = timefullfft;
    cands[ii].full_lo_r = lorfullfft;
    cands[ii].mini_N = fftlen;
    cands[ii].psr_p = timefullfft / (lorfullfft + numminifft);
    cands[ii].orb_p = timefullfft * cands[ii].mini_r / fftlen;
  }
}


static int padfftlen(int minifftlen, int numbetween, int *padlen)
/* Choose a good (easily factorable) FFT length and an */
/* appropriate padding length (for low accuracy work). */
/* We assume that minifftlen is a power-of-2...        */
{
  int lowaccbins, newlen;

  /* First choose an appropriate number of full pad bins */

  *padlen = minifftlen / 8;
  lowaccbins = r_resp_halfwidth(LOWACC) * (numbetween / 2);
  if (*padlen > lowaccbins) *padlen = lowaccbins;

  /* Now choose the FFT length (This requires an FFT that */
  /* can perform non-power-of-two FFTs -- USE FFTW!!!     */

  newlen = (minifftlen + *padlen) * numbetween;

  if (newlen <= 144) return newlen;
  else if (newlen <= 288) return 288;
  else if (newlen <= 540) return 540;
  else if (newlen <= 1080) return 1080;
  else if (newlen <= 2100) return 2100;
  else if (newlen <= 4200) return 4200;
  else if (newlen <= 8232) return 8232;
  else if (newlen <= 16464) return 16464;
  else if (newlen <= 32805) return 32805;
  else if (newlen <= 65610) return 65610;
  else if (newlen <= 131220) return 131220;
  else if (newlen <= 262440) return 262440;
  else if (newlen <= 525000) return 525000;
  else if (newlen <= 1050000) return 1050000;
  /* The following might get optimized out and give garbage... */
  else return (int)((int)((newlen + 1000)/1000) * 1000.0);
}

void print_rawbincand(rawbincand cand){
  printf("  Sigma       =  %-7.3f\n", cand.mini_sigma);
  printf("  Orbit p     =  %-8.2f\n", cand.orb_p);
  if (cand.psr_p < 0.001)
    printf("  Pulsar p    =  %-12.5e\n", cand.psr_p);
  else
    printf("  Pulsar p    =  %-12.9f\n", cand.psr_p);
  printf("  rlo (full)  =  %-10.0f\n", cand.full_lo_r);
  printf("  N (mini)    =  %-6.0f\n", cand.mini_N);
  printf("  r (detect)  =  %-9.3f\n", cand.mini_r);
  printf("  Power       =  %-8.3f\n", cand.mini_power);
  printf("  Numsum      =  %-2.0f\n", cand.mini_numsum);
  printf("  N (full)    =  %-10.0f\n", cand.full_N);
  printf("  T (full)    =  %-13.6f\n\n", cand.full_T);
}

float percolate_fftcands(fftcand *cands, int numcands)
  /*  Pushes a fftcand candidate as far up the array of   */
  /*  candidates as it shoud go to keep the array sorted  */
  /*  in indecreasing powers.  Returns the new lowest     */
  /*  power in the array.                                 */
{
  int ii;
  fftcand tempzz;

  for (ii = numcands - 2; ii >= 0; ii--) {
    if (cands[ii].p < cands[ii + 1].p) {
      SWAP(cands[ii], cands[ii + 1]);
    } else {
      break;
    }
  }
  return cands[numcands - 1].p;
}


float percolate_rawbincands(rawbincand *cands, int numcands)
  /*  Pushes a rawbincand candidate as far up the array of   */
  /*  candidates as it shoud go to keep the array sorted in  */
  /*  indecreasing significance.  Returns the new lowest     */
  /*  sigma in the array.                                    */
{
  int ii;
  rawbincand tempzz;

  for (ii = numcands - 2; ii >= 0; ii--) {
    if (cands[ii].mini_sigma < cands[ii + 1].mini_sigma) {
      SWAP(cands[ii], cands[ii + 1]);
    } else {
      break;
    }
  }
  return cands[numcands - 1].mini_sigma;
}


int not_already_there_rawbin(rawbincand newcand, 
			     rawbincand *list, int nlist)
{
  int ii;

  /* Loop through the candidates already in the list */

  for (ii = 0; ii < nlist; ii++) {
    if (list[ii].mini_sigma == 0.0)
      break;

    /* Do not add the candidate to the list if it is a lower power */
    /* version of an already listed candidate.                     */

    if (list[ii].mini_N == newcand.mini_N) {
      if (fabs(list[ii].mini_r - newcand.mini_r) < 0.6) {
	if (list[ii].mini_sigma > newcand.mini_sigma) {
	  return 0;
	}
      }
    }
  }
  return 1;
}


void compare_rawbin_cands(rawbincand *list, int nlist, 
			  char *notes)
{
  double perr;
  int ii, jj, kk, ll;
  char tmp[30];

  /* Loop through the candidates (reference cands) */

  for (ii = 0; ii < nlist; ii++) {

    /* Loop through the candidates (referenced cands) */

    for (jj = 0; jj < nlist; jj++) {
      if (ii == jj)
	continue;
      perr = 0.5 * list[jj].full_T / list[jj].mini_N;

      /* Loop through the possible PSR period harmonics */
      
      for (kk = 1; kk < 41; kk++) {
	
	/* Check if the PSR Fourier freqs are close enough */

	if (fabs(list[ii].full_lo_r - list[jj].full_lo_r / kk) < 
	    list[ii].mini_N) {

	  /* Loop through the possible binary period harmonics */

	  for (ll = 1; ll < 10; ll++) {

	    /* Check if the binary Fourier freqs are close enough */

	    if (fabs(list[ii].orb_p - list[jj].orb_p / ll) < perr) {

	      /* Check if the note has already been written */

	      sprintf(tmp, "%.18s", notes + jj * 18);
	      if (!strcmp("                  ", tmp)) {

		/* Write the note */

		if (ll == 1 && kk == 1)
		  sprintf(notes + jj * 18, "Same as #%d?", ii + 1);
		else 
		  sprintf(notes + jj * 18, "MH=%d H=%d of #%d", ll, kk, ii + 1);

		break;
	      }
	    }
	  }
	}
      }
    }
  }
}


int comp_rawbin_to_cand(rawbincand *cand, infodata *idata,
			char *output, int full)
  /* Compares a binary PSR candidate defined by its props found in    */
  /*   *cand, and *idata with all of the pulsars in the pulsar        */
  /*   database.  It returns a string (verbose if full==1) describing */
  /*   the results of the search in *output.                          */
{
  int i, j, k;
  static int np = 0;
  static psrdatabase pdata;
  double T, theop, ra, dec, beam2, difft = 0.0, epoch;
  double bmod, pmod, orbperr, psrperr;
  char tmp1[80], tmp2[80], tmp3[80], tmp4[20];

  /* If calling for the first time, read the database. */

  if (!np)
    np = read_database(&pdata);

  /* Convert the beam width to radians */

  beam2 = 2.0 * ARCSEC2RAD * idata->fov;
  
  /* Convert RA and DEC to radians  (Use J2000) */

  ra = hms2rad(idata->ra_h, idata->ra_m, idata->ra_s);
  dec = dms2rad(idata->dec_d, idata->dec_m, idata->dec_s);

  /* Calculate the time related variables  */

  T = idata->N * idata->dt;
  epoch = (double) idata->mjd_i + idata->mjd_f;
  
  /* Calculate the approximate error in our value of orbital period */
  
  orbperr = 0.5 * cand->full_T / cand->mini_N;
  
  /* Calculate the approximate error in our value of spin period */

  if (cand->full_lo_r == 0.0)
    psrperr = cand->psr_p;
  else 
    psrperr = fabs(cand->full_T / 
		   (cand->full_lo_r + 0.5 * cand->mini_N) -
		   cand->full_T / cand->full_lo_r);

  /* Run through RAs in database looking for things close  */
  /* If find one, check the DEC as well (the angle between */
  /* the sources < 2*beam diam).  If find one, check its   */
  /* period.  If this matches within 2*perr, return the    */
  /* number of the pulsar.  If no matches, return 0.       */

  for (i = 0; i < np; i++) {
    
    /* See if we're close in RA */
    
    if (fabs(pdata.ra2000[i] - ra) < 5.0 * beam2) {
      
      /* See if we're close in RA and DEC */
      
      if (sphere_ang_diff(pdata.ra2000[i], pdata.dec2000[i], \
			  ra, dec) < 5.0 * beam2) {

	/* Check that the psr in the database is in a binary   */

	if (pdata.ntype[i] & 8) {

	  /* Predict the period of the pulsar at the observation MJD */

	  difft = SECPERDAY * (epoch - pdata.epoch[i]);
	  theop = pdata.p[i] + pdata.pdot[i] * difft;
	  sprintf(tmp4, "%.8s", pdata.bname + i * 8);

	  /* Check the predicted period and its harmonics against the */
	  /* measured period.  Use both pulsar and binary periods.    */

	  for (j = 1; j < 41; j++) {
	    pmod = 1.0 / (double) j;
	    if (fabs(theop * pmod - cand->psr_p) < psrperr) {
	      for (k = 1; k < 10; k++) {
		bmod = (double) k;
		if (fabs(pdata.pb[i] * bmod - cand->orb_p / SECPERDAY) < orbperr) {
		  if (!strcmp("        ", tmp4)) {
		    if (j > 1) {
		      if (full) {
			sprintf(tmp1, "Possibly the %s phasemod harmonic ", num[k]);
			sprintf(tmp2, "of the %s harmonic of PSR ", num[j]);
			sprintf(tmp3, "J%.12s (p = %11.7f s, pbin = %9.4f d).\n", \
				pdata.jname + i * 12, theop, pdata.pb[i]);
			sprintf(output, "%s%s%s", tmp1, tmp2, tmp3);
		      } else {
			sprintf(output, "%s H J%.12s", num[k], pdata.jname + i * 12);
		      }
		    } else {
		      if (full) {
			sprintf(tmp1, "Possibly the %s phasemod harmonic ", num[k]);
			sprintf(tmp2, "of PSR J%.12s (p = %11.7f s, pbin = %9.4f d).\n", \
				pdata.jname + i * 12, theop, pdata.pb[i]);
			sprintf(output, "%s%s", tmp1, tmp2);
		      } else {
			sprintf(output, "PSR J%.12s", pdata.jname + i * 12);
		      }
		    }
		  } else {
		    if (j > 1) {
		      if (full) {
			sprintf(tmp1, "Possibly the %s modulation harmonic ", num[k]);
			sprintf(tmp2, "of the %s harmonic of PSR ", num[j]);
			sprintf(tmp3, "B%s (p = %11.7f s, pbin = %9.4f d).\n", \
				tmp4, theop, pdata.pb[i]);
			sprintf(output, "%s%s%s", tmp1, tmp2, tmp3);
		      } else {
			sprintf(output, "%s H B%s", num[k], tmp4);
		      }
		    } else {
		      if (full) {
			sprintf(tmp1, "Possibly the %s phasemod harmonic ", num[k]);
			sprintf(tmp2, "of PSR B%s (p = %11.7f s, pbin = %9.4f d).\n", \
				tmp4, theop, pdata.pb[i]);
			sprintf(output, "%s%s", tmp1, tmp2);
		      } else {
			sprintf(output, "PSR B%s", tmp4);
		      }
		    }
		  }
		}
		return i + 1;
	      }
	    }
	  }
	}
      }
    }
  }

  /* Didn't find a match */

  if (full) {
    sprintf(output, "I don't recognize this candidate in the pulsar database.\n");
  } else {
    sprintf(output, "                  ");
  }
  return 0;
}


void file_rawbin_candidates(rawbincand *cand, char *notes,
			    int numcands, char name[])
/* Outputs a .ps file describing all the binary candidates from a    */
/*   binary search. */
{
  FILE *fname;
  int i, j, k = 0;
  int nlines = 87, pages, extralines, linestoprint;
  char filenm[100], infonm[100], command[200];
  double orbperr, psrperr;
  
  sprintf(filenm, "%s_bin", name);
  sprintf(infonm, "%s.inf", name);
  fname = chkfopen(filenm, "w");

  if (numcands <= 0) {
    printf(" Must have at least 1 candidate in ");
    printf("file_bin_candidates().\n\n");
    exit(1);
  }
  pages = numcands / nlines + 1;
  extralines = numcands % nlines;

  for (i = 1; i <= pages; i++) {

    /*                       1         2         3         4         5         6         7         8         9         0         1    */  
    /*              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234*/
    fprintf(fname, "#               P_orbit +/- Error   P_pulsar +/- Error   FullFFT   MiniFFT   MiniFFT  Num   Sum                   \n");
    fprintf(fname, "# Cand  Sigma         (sec)                (sec)         Low Bin   Length      Bin    Sum  Power  Notes           \n");
    fprintf(fname, "#------------------------------------------------------------------------------------------------------------------\n");
    
    if (i == pages) {
      linestoprint = extralines;
    } else {
      linestoprint = nlines;
    }
    
    for (j = 0; j < linestoprint; j++, k++) {
      
      /* Calculate the approximate error in our value of orbital period */
      orbperr = 0.5 * cand[k].full_T / cand[k].mini_N;
      
      /* Calculate the approximate error in our value of spin period */

      if (cand[k].full_lo_r == 0.0)
	psrperr = cand[k].psr_p;
      else 
	psrperr = fabs(cand[k].full_T / (cand[k].full_lo_r + 
					 0.5 * cand[k].mini_N) -
		       cand[k].full_T / cand[k].full_lo_r);
      
      /*  Now output it... */

      fprintf(fname, " %4d %7.3f  ", k + 1, cand[k].mini_sigma);
      fprintf(fname, " %8.2f", cand[k].orb_p);
      fprintf(fname, " %-7.2g ", orbperr);
      if (cand[k].psr_p < 0.001)
	fprintf(fname, " %12.5e", cand[k].psr_p);
      else
	fprintf(fname, " %12.9f", cand[k].psr_p);
      fprintf(fname, " %-7.2g ", psrperr);
      fprintf(fname, " %9.0f  ", cand[k].full_lo_r);
      fprintf(fname, " %6.0f ", cand[k].mini_N);
      fprintf(fname, " %8.1f ", cand[k].mini_r);
      fprintf(fname, " %2.0f ", cand[k].mini_numsum);
      fprintf(fname, "%7.2f ", cand[k].mini_power);
      fprintf(fname, " %.18s\n", notes + k * 18);
      fflush(fname);
    }
  }
  fprintf(fname, "\n Notes:  MH = Modulation harmonic.  ");
  fprintf(fname, "H = Pulsar harmonic.  # indicates the candidate number.\n\n");
  fclose(fname);
  sprintf(command, "cat %s >> %s", infonm, filenm);
  system(command);
  sprintf(command, \
	  "$PRESTO/bin/a2x -c1 -n90 -title -date -num %s > %s.ps", \
	  filenm, filenm);
  system(command);
}

