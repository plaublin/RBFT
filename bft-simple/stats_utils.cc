#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "stats_utils.h"

/* compute the average of the list values */
float vector_compute_avg(vector<float> *v)
{
  vector<float>::iterator it;
  float sum=0;
  for (it=v->begin(); it != v->end(); it++) {
    sum += *it;
  }

  return sum / v->size();
}

/* compute the standard deviation of the list values */
float vector_compute_stddev(vector<float>* v, float avg)
{
  float sum, tmp;
  vector<float>::iterator it;

  sum = 0;
  for (it = v->begin(); it != v->end(); it++)
  {
    tmp = *it - avg;
    sum += tmp * tmp;
  }

  return sqrtf(sum / v->size());
}
