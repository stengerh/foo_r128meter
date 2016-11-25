/* See COPYING file for copyright and license details. */

#include "ebur128.h"

#define _USE_MATH_DEFINES
#include <float.h>
#include <limits.h>
#include <limits>
#include <math.h> /* You may have to define _USE_MATH_DEFINES if you use MSVC */
#include <stdio.h>
#include <stdlib.h>

/* This can be replaced by any BSD-like queue implementation. */
#include "queue.h"

#define PI 3.14159265358979323846

#define CHECK_ERROR(condition, message, errorcode, goto_point)                 \
  if ((condition)) {                                                           \
    fprintf(stderr, message);                                                  \
    errcode = (errorcode);                                                     \
    goto goto_point;                                                           \
  }

/* Those will be calculated when initializing the library */
static double minus_eight_decibels;
static double minus_twenty_decibels;
static double abs_threshold_energy;

int ebur128_init_multi_array(double*** v, size_t channels, size_t filter_size) {
  size_t i;
  int errcode = 0;
  *v = (double**) calloc(channels, sizeof(double*));
  CHECK_ERROR(!(*v), "Could not allocate memory!\n", 1, exit)
  for (i = 0; i < channels; ++i) {
    (*v)[i] = (double*) calloc(filter_size, sizeof(double));
    CHECK_ERROR(!((*v)[i]), "Could not allocate memory!\n", 1, free_all)
  }
  return 0;

free_all:
  for (i = 0; i < channels; ++i) {
    free((*v)[i]);
  }
  free(*v);
exit:
  return errcode;
}

void ebur128_release_multi_array(double*** v, size_t channels) {
  size_t i;
  for (i = 0; i < channels; ++i) {
    free((*v)[i]);
  }
  free(*v);
  *v = NULL;
}

int ebur128_init_filter(ebur128_state* st) {
  int errcode = 0;

  double f0 = 1681.974450955533;
  double G  =    3.999843853973347;
  double Q  =    0.7071752369554196;

  double K      = tan(PI * f0 / (double) st->samplerate);
  double Vh     = pow(10, G / 20.0);
  double Vb     = pow(Vh, 0.4996667741545416);

  double b1[] = {0.0, 0.0, 0.0};
  double a1[] = {1.0, 0.0, 0.0};
  double a0 = 1 + K / Q + K * K;
  b1[0] = (Vh + Vb * K / Q + K * K) / a0;
  b1[1] =          2 * (K * K - Vh) / a0;
  b1[2] = (Vh - Vb * K / Q + K * K) / a0;
  a1[1] =          2 * (K * K -  1) / a0;
  a1[2] =       (1 - K / Q + K * K) / a0;

  /* fprintf(stderr, "%.14f %.14f %.14f %.14f %.14f\n",
                     b1[0], b1[1], b1[2], a1[1], a1[2]); */

  {
  double b2[] = {1.0, -2.0, 1.0};
  double a2[] = {1.0, 0.0, 0.0};

  f0 = 38.13547087602444;
  Q  =  0.5003270373238773;
  K = tan(PI * f0 / (double) st->samplerate);

  a2[1] =     2 * (K * K - 1) / (1 + K / Q + K * K);
  a2[2] = (1 - K / Q + K * K) / (1 + K / Q + K * K);

  /* fprintf(stderr, "%.14f %.14f\n", a2[1], a2[2]); */


  st->a = (double*) calloc(5, sizeof(double));
  CHECK_ERROR(errcode, "Could not allocate memory!\n", 1, exit)
  st->b = (double*) calloc(5, sizeof(double));
  CHECK_ERROR(errcode, "Could not allocate memory!\n", 1, free_a)

  st->b[0] = b1[0] * b2[0];
  st->b[1] = b1[0] * b2[1] + b1[1] * b2[0];
  st->b[2] = b1[0] * b2[2] + b1[1] * b2[1] + b1[2] * b2[0];
  st->b[3] = b1[1] * b2[2] + b1[2] * b2[1];
  st->b[4] = b1[2] * b2[2];

  st->a[0] = a1[0] * a2[0];
  st->a[1] = a1[0] * a2[1] + a1[1] * a2[0];
  st->a[2] = a1[0] * a2[2] + a1[1] * a2[1] + a1[2] * a2[0];
  st->a[3] = a1[1] * a2[2] + a1[2] * a2[1];
  st->a[4] = a1[2] * a2[2];
  }

  return 0;

free_a:
  free(st->a);
exit:
  return errcode;
}

int ebur128_init_channel_map(ebur128_state* st) {
  size_t i;
  st->channel_map = (int*) calloc(st->channels, sizeof(int));
  if (!st->channel_map) return 1;
  for (i = 0; i < st->channels; ++i) {
    switch (i) {
      case 0:  st->channel_map[i] = EBUR128_LEFT;           break;
      case 1:  st->channel_map[i] = EBUR128_RIGHT;          break;
      case 2:  st->channel_map[i] = EBUR128_CENTER;         break;
      case 3:  st->channel_map[i] = EBUR128_UNUSED;         break;
      case 4:  st->channel_map[i] = EBUR128_LEFT_SURROUND;  break;
      case 5:  st->channel_map[i] = EBUR128_RIGHT_SURROUND; break;
      default: st->channel_map[i] = EBUR128_UNUSED;         break;
    }
  }
  return 0;
}

ebur128_state* ebur128_init(int channels, int samplerate, size_t mode) {
  int errcode;
  ebur128_state* state;

  state = (ebur128_state*) malloc(sizeof(ebur128_state));
  CHECK_ERROR(!state, "Could not allocate memory!\n", 0, exit)
  state->channels = (size_t) channels;
  errcode = ebur128_init_channel_map(state);
  CHECK_ERROR(errcode, "Could not initialize channel map!\n", 0, free_state)
  state->samplerate = (size_t) samplerate;
  state->mode = mode;
  if ((mode & EBUR128_MODE_S) == EBUR128_MODE_S) {
    state->audio_data_frames = state->samplerate * 3;
  } else if ((mode & EBUR128_MODE_M) == EBUR128_MODE_M) {
    state->audio_data_frames = state->samplerate / 5 * 2;
  } else {
    return NULL;
  }
  state->audio_data = (double*) calloc(state->audio_data_frames *
                                       state->channels,
                                       sizeof(double));
  CHECK_ERROR(!state->audio_data, "Could not allocate memory!\n", 0,
                                  free_channel_map)
  errcode = ebur128_init_multi_array(&(state->v), state->channels, 5);
  CHECK_ERROR(errcode, "Could not allocate memory!\n", 0, free_audio_data)
  errcode = ebur128_init_filter(state);
  CHECK_ERROR(errcode, "Could not initialize filter!\n", 0, free_v)

  SLIST_INIT(&state->block_list);
  SLIST_INIT(&state->short_term_block_list);
  state->short_term_frame_counter = 0;
  state->block_counter = 0;

  /* the first block needs 400ms of audio data */
  state->needed_frames = state->samplerate / 5 * 2;
  /* start at the beginning of the buffer */
  state->audio_data_index = 0;

  /* initialize static constants */
  minus_eight_decibels = pow(10, -8.0 / 10.0);
  minus_twenty_decibels = pow(10, -20.0 / 10.0);
  abs_threshold_energy = pow(10.0, (-70.0 + 0.691) / 10.0);

  return state;

free_v:
  ebur128_release_multi_array(&(state->v), state->channels);
free_audio_data:
  free(state->audio_data);
free_channel_map:
  free(state->channel_map);
free_state:
  free(state);
exit:
  return NULL;
}

int ebur128_destroy(ebur128_state** st) {
  struct ebur128_dq_entry* entry;
  free((*st)->audio_data);
  free((*st)->channel_map);
  ebur128_release_multi_array(&(*st)->v, (*st)->channels);
  free((*st)->a);
  free((*st)->b);
  while (!SLIST_EMPTY(&(*st)->block_list)) {
    entry = SLIST_FIRST(&(*st)->block_list);
    SLIST_REMOVE_HEAD(&(*st)->block_list, entries);
    free(entry);
  }
  while (!SLIST_EMPTY(&(*st)->short_term_block_list)) {
    entry = SLIST_FIRST(&(*st)->short_term_block_list);
    SLIST_REMOVE_HEAD(&(*st)->short_term_block_list, entries);
    free(entry);
  }

  free(*st);
  *st = NULL;

  return 0;
}

#define EBUR128_FILTER(type, min_scale, max_scale)                             \
int ebur128_filter_##type(ebur128_state* st, const type* src, size_t frames) { \
  static double scaling_factor = -((double) min_scale) > (double) max_scale ?  \
                                 -((double) min_scale) : (double) max_scale;   \
  double* audio_data = st->audio_data + st->audio_data_index;                  \
  size_t i, c;                                                                 \
  for (c = 0; c < st->channels; ++c) {                                         \
    if (st->channel_map[c] == EBUR128_UNUSED) continue;                        \
    for (i = 0; i < frames; ++i) {                                             \
      st->v[c][0] = src[i * st->channels + c] / scaling_factor                 \
                  - st->a[1] * st->v[c][1]                                     \
                  - st->a[2] * st->v[c][2]                                     \
                  - st->a[3] * st->v[c][3]                                     \
                  - st->a[4] * st->v[c][4];                                    \
      audio_data[i * st->channels + c] =                                       \
                    st->b[0] * st->v[c][0]                                     \
                  + st->b[1] * st->v[c][1]                                     \
                  + st->b[2] * st->v[c][2]                                     \
                  + st->b[3] * st->v[c][3]                                     \
                  + st->b[4] * st->v[c][4];                                    \
      st->v[c][4] = st->v[c][3];                                               \
      st->v[c][3] = st->v[c][2];                                               \
      st->v[c][2] = st->v[c][1];                                               \
      st->v[c][1] = st->v[c][0];                                               \
    }                                                                          \
  }                                                                            \
  return 0;                                                                    \
}
EBUR128_FILTER(short, SHRT_MIN, SHRT_MAX)
EBUR128_FILTER(int, INT_MIN, INT_MAX)
EBUR128_FILTER(float, -1.0f, 1.0f)
EBUR128_FILTER(double, -1.0, 1.0)

int ebur128_calc_gating_block(ebur128_state* st, size_t frames_per_block,
                              double* optional_output) {
  size_t i, c;
  double sum = 0.0;
  double channel_sum;
  for (c = 0; c < st->channels; ++c) {
    if (st->channel_map[c] == EBUR128_UNUSED) continue;
    channel_sum = 0.0;
    if (st->audio_data_index < frames_per_block * st->channels) {
      for (i = 0; i < st->audio_data_index / st->channels; ++i) {
        channel_sum += st->audio_data[i * st->channels + c] *
                       st->audio_data[i * st->channels + c];
      }
      for (i = st->audio_data_frames -
              (frames_per_block -
               st->audio_data_index / st->channels);
           i < st->audio_data_frames; ++i) {
        channel_sum += st->audio_data[i * st->channels + c] *
                       st->audio_data[i * st->channels + c];
      }
    } else {
      for (i = st->audio_data_index / st->channels - frames_per_block;
           i < st->audio_data_index / st->channels;
           ++i) {
        channel_sum += st->audio_data[i * st->channels + c] *
                       st->audio_data[i * st->channels + c];
      }
    }
    if (st->channel_map[c] == EBUR128_LEFT_SURROUND ||
        st->channel_map[c] == EBUR128_RIGHT_SURROUND) {
      channel_sum *= 1.41;
    }
    sum += channel_sum;
  }
  sum /= (double) frames_per_block;
  if (optional_output) {
    *optional_output = sum;
    return 0;
  } else if (sum >= abs_threshold_energy) {
    struct ebur128_dq_entry* block;
    block = (struct ebur128_dq_entry*) malloc(sizeof(struct ebur128_dq_entry));
    if (!block) return -1;
    block->z = sum;
    SLIST_INSERT_HEAD(&st->block_list, block, entries);
    ++st->block_counter;
    return 0;
  } else {
    return 1;
  }
}

void ebur128_set_channel_map(ebur128_state* st,
                            int* channel_map) {
  memcpy(st->channel_map, channel_map, st->channels * sizeof(int));
}

int ebur128_set_channel(ebur128_state* st, int channel_number, int value) {
  if (channel_number < 0 || (size_t) channel_number >= st->channels) {
    return 1;
  }
  st->channel_map[channel_number] = value;
  return 0;
}

int ebur128_change_parameters(ebur128_state* st,
                              int channels,
                              int samplerate) {
  int errcode;
  if ((size_t) channels == st->channels &&
      (size_t) samplerate == st->samplerate) {
    return 2;
  }
  free(st->audio_data);
  ebur128_release_multi_array(&(st->v), st->channels);

  if ((size_t) channels != st->channels) {
    free(st->channel_map);
    st->channels = (size_t) channels;
    errcode = ebur128_init_channel_map(st);
    CHECK_ERROR(errcode, "Could not initialize channel map!\n", 1, exit)
  }
  if ((size_t) samplerate != st->samplerate) {
    st->samplerate = (size_t) samplerate;
    free(st->a);
    free(st->b);
    errcode = ebur128_init_filter(st);
    CHECK_ERROR(errcode, "Could not initialize filter!\n", 1, free_channel_map)
  }
  if ((st->mode & EBUR128_MODE_S) == EBUR128_MODE_S) {
    st->audio_data_frames = st->samplerate * 3;
  } else if ((st->mode & EBUR128_MODE_M) == EBUR128_MODE_M) {
    st->audio_data_frames = st->samplerate / 5 * 2;
  } else {
    return 1;
  }
  st->audio_data = (double*) calloc(st->audio_data_frames *
                                    st->channels,
                                    sizeof(double));
  CHECK_ERROR(!st->audio_data, "Could not allocate memory!\n", 1,
                               free_filter_coeffs)
  errcode = ebur128_init_multi_array(&(st->v), st->channels, 5);
  CHECK_ERROR(errcode, "Could not allocate memory!\n", 1, free_audio_data)

  /* the first block needs 400ms of audio data */
  st->needed_frames = st->samplerate / 5 * 2;
  /* start at the beginning of the buffer */
  st->audio_data_index = 0;
  /* reset short term frame counter */
  st->short_term_frame_counter = 0;

  return 0;

free_audio_data:
  free(st->audio_data);
  st->audio_data = NULL;
free_filter_coeffs:
  free(st->a);
  free(st->b);
  st->a = NULL;
  st->b = NULL;
free_channel_map:
  free(st->channel_map);
  st->channel_map = NULL;
exit:
  return 1;
}


double ebur128_energy_shortterm(ebur128_state* st);
#define EBUR128_ADD_FRAMES(type)                                               \
int ebur128_add_frames_##type(ebur128_state* st,                               \
                              const type* src, size_t frames) {                \
  int errcode = 0;                                                             \
  size_t src_index = 0;                                                        \
  while (frames > 0) {                                                         \
    if (frames >= st->needed_frames) {                                         \
      ebur128_filter_##type(st, src + src_index, st->needed_frames);           \
      src_index += st->needed_frames * st->channels;                           \
      frames -= st->needed_frames;                                             \
      st->audio_data_index += st->needed_frames * st->channels;                \
      /* calculate the new gating block */                                     \
      if ((st->mode & EBUR128_MODE_I) == EBUR128_MODE_I) {                     \
        errcode = ebur128_calc_gating_block(st, st->samplerate / 5 * 2, NULL); \
        if (errcode == -1) return 1;                                           \
      }                                                                        \
      if ((st->mode & EBUR128_MODE_LRA) == EBUR128_MODE_LRA) {                 \
        st->short_term_frame_counter += st->needed_frames;                     \
        if (st->short_term_frame_counter == st->samplerate * 3) {              \
          double st_energy = ebur128_energy_shortterm(st);                     \
          struct ebur128_dq_entry* block;                                      \
          block = (struct ebur128_dq_entry*)                                   \
                  malloc(sizeof(struct ebur128_dq_entry));                     \
          if (!block) return 1;                                                \
          block->z = st_energy;                                                \
          SLIST_INSERT_HEAD(&st->short_term_block_list, block, entries);       \
          st->short_term_frame_counter = st->samplerate * 2;                   \
        }                                                                      \
      }                                                                        \
      /* 200ms are needed for all blocks besides the first one */              \
      st->needed_frames = st->samplerate / 5;                                  \
      /* reset audio_data_index when buffer full */                            \
      if (st->audio_data_index == st->audio_data_frames * st->channels) {      \
        st->audio_data_index = 0;                                              \
      }                                                                        \
    } else {                                                                   \
      ebur128_filter_##type(st, src + src_index, frames);                      \
      st->audio_data_index += frames * st->channels;                           \
      if ((st->mode & EBUR128_MODE_LRA) == EBUR128_MODE_LRA) {                 \
        st->short_term_frame_counter += frames;                                \
      }                                                                        \
      st->needed_frames -= frames;                                             \
      frames = 0;                                                              \
    }                                                                          \
  }                                                                            \
  return 0;                                                                    \
}
EBUR128_ADD_FRAMES(short)
EBUR128_ADD_FRAMES(int)
EBUR128_ADD_FRAMES(float)
EBUR128_ADD_FRAMES(double)

void ebur128_start_new_segment(ebur128_state* st) {
  st->block_counter = 0;
  /* the first block needs 400ms of audio data */
  st->needed_frames = st->samplerate / 5 * 2;
  /* start at the beginning of the buffer */
  st->audio_data_index = 0;
  memset(st->audio_data, '\0', st->audio_data_frames *
                               st->channels *
                               sizeof(double));
  /* reset short term frame counter */
  st->short_term_frame_counter = 0;
}

double ebur128_energy_to_loudness(double energy) {
  return 10 * (log(energy) / log(10.0)) - 0.691;
}

double ebur128_gated_loudness(ebur128_state** sts, size_t size,
                              size_t block_count) {
  struct ebur128_dq_entry* it;
  double relative_threshold = 0.0;
  double gated_loudness = 0.0;
  size_t above_thresh_counter = 0;
  size_t i;

  for (i = 0; i < size; i++) {
    if ((sts[i]->mode & EBUR128_MODE_I) != EBUR128_MODE_I) {
      return std::numeric_limits<double>::quiet_NaN();
    }
  }

  for (i = 0; i < size; i++) {
    SLIST_FOREACH(it, &sts[i]->block_list, entries) {
      if (above_thresh_counter >= block_count) break;
      ++above_thresh_counter;
      relative_threshold += it->z;
    }
  }
  if (!above_thresh_counter) return std::numeric_limits<double>::infinity();
  relative_threshold /= (double) above_thresh_counter;
  relative_threshold *= minus_eight_decibels;
  above_thresh_counter = 0;
  for (i = 0; i < size; i++) {
    SLIST_FOREACH(it, &sts[i]->block_list, entries) {
      if (block_count == 0) break;
      if (it->z >= relative_threshold) {
        ++above_thresh_counter;
        gated_loudness += it->z;
      }
      --block_count;
    }
  }
  if (!above_thresh_counter) return std::numeric_limits<double>::infinity();
  gated_loudness /= (double) above_thresh_counter;
  return ebur128_energy_to_loudness(gated_loudness);
}

double ebur128_loudness_global(ebur128_state* st) {
  return ebur128_gated_loudness(&st, 1, (size_t) -1);
}

double ebur128_loudness_segment(ebur128_state* st) {
  return ebur128_gated_loudness(&st, 1, st->block_counter);
}

double ebur128_loudness_global_multiple(ebur128_state** sts, size_t size) {
  return ebur128_gated_loudness(sts, size, (size_t) -1);
}

double ebur128_loudness_momentary(ebur128_state* st) {
  return ebur128_gated_loudness(&st, 1, 1);
}

double ebur128_loudness_shortterm(ebur128_state* st) {
  return ebur128_gated_loudness(&st, 1, 8);
}

void ebur128_gated_loudness_cleanup(ebur128_state* st,
                              size_t block_count) {
  struct ebur128_dq_entry* it;
  double relative_threshold = 0.0;
  size_t above_thresh_counter = 0;

  if ((st->mode & EBUR128_MODE_I) != EBUR128_MODE_I) {
      return;
  }

  SLIST_FOREACH(it, &st->block_list, entries) {
    if (above_thresh_counter >= block_count) break;
    ++above_thresh_counter;
    relative_threshold += it->z;
  }
  if (!above_thresh_counter) return;
  relative_threshold /= (double) above_thresh_counter;
  relative_threshold *= minus_eight_decibels;
  above_thresh_counter = 0;
  SLIST_FOREACH(it, &st->block_list, entries) {
    if (block_count == 0) break;
    if (it->z >= relative_threshold) {
      ++above_thresh_counter;
    }
    --block_count;
  }
  while (it) {
    struct ebur128_dq_entry* next = SLIST_NEXT(it, entries);
    SLIST_REMOVE(&st->block_list, it, ebur128_dq_entry, entries);
    free(it);
    it = next;
  }
}

double ebur128_energy_in_interval(ebur128_state* st, size_t interval_frames) {
  double loudness;

  if (interval_frames > st->audio_data_frames) return std::numeric_limits<double>::quiet_NaN();
  ebur128_calc_gating_block(st, interval_frames, &loudness);
  return loudness;
}

double ebur128_energy_shortterm(ebur128_state* st) {
  return ebur128_energy_in_interval(st, st->samplerate * 3);
}

static int ebur128_double_cmp(const void *p1, const void *p2) {
  const double* d1 = (const double*) p1;
  const double* d2 = (const double*) p2;
  return *d1 > *d2;
}

/* EBU - TECH 3342 */
double ebur128_loudness_range(ebur128_state* st) {
  size_t i;
  struct ebur128_dq_entry* it;
  double* stl_vector;
  size_t stl_size = 0;
  double* stl_abs_gated;
  size_t stl_abs_gated_size;
  double* stl_relgated;
  size_t stl_relgated_size;
  double stl_power = 0.0, stl_integrated;
  /* High and low percentile energy */
  double h_en, l_en;

  if ((st->mode & EBUR128_MODE_LRA) != EBUR128_MODE_LRA) return std::numeric_limits<double>::quiet_NaN();

  SLIST_FOREACH(it, &st->short_term_block_list, entries) {
    ++stl_size;
  }
  if (!stl_size) return 0.0;
  stl_vector = (double*) calloc(stl_size, sizeof(double));
  if (!stl_vector) return std::numeric_limits<double>::quiet_NaN();
  i = 0;
  SLIST_FOREACH(it, &st->short_term_block_list, entries) {
    stl_vector[i] = it->z;
    ++i;
  }
  qsort(stl_vector, stl_size, sizeof(double), ebur128_double_cmp);
  stl_abs_gated = stl_vector;
  stl_abs_gated_size = stl_size;
  while (stl_abs_gated_size > 0 && *stl_abs_gated < abs_threshold_energy) {
    ++stl_abs_gated;
    --stl_abs_gated_size;
  }
  for (i = 0; i < stl_abs_gated_size; ++i) {
    stl_power += stl_abs_gated[i];
  }
  stl_power /= (double) stl_abs_gated_size;
  stl_integrated = minus_twenty_decibels * stl_power;

  stl_relgated = stl_abs_gated;
  stl_relgated_size = stl_abs_gated_size;
  while (stl_relgated_size > 0 && *stl_relgated < stl_integrated) {
    ++stl_relgated;
    --stl_relgated_size;
  }

  h_en = stl_relgated[(size_t) ((double) (stl_relgated_size - 1) * 0.95 + 0.5)];
  l_en = stl_relgated[(size_t) ((double) (stl_relgated_size - 1) * 0.1 + 0.5)];
  free(stl_vector);
  return ebur128_energy_to_loudness(h_en) - ebur128_energy_to_loudness(l_en);
}
