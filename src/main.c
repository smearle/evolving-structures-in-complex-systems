#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "automaton/2d_automaton.h"
#include "automaton/rule.h"
#include "automaton/wolfram_automaton.h"
#include "utils/utils.h"

#define RATE 0.01

const char help[] = "Use with either 2d or 1d as first argument";

struct str
{
  double value;
  int index;
};

int cmp(const void *a, const void *b)
{
  struct str *a1 = (struct str *)a;
  struct str *a2 = (struct str *)b;
  if ((*a1).value > (*a2).value)
    return -1;
  else if ((*a1).value < (*a2).value)
    return 1;
  else
    return 0;
}


void iterative_search(int n_simulations, int input_flag,
                      long timesteps, uint64_t grule_size,
                      uint8_t* rule_array, char* rule_buf,
                      struct Options2D* opts)
{
  FILE* genealogy_file;
  char* genealogy_fname;

  int population_size = 5;
  int n_children = 5;
  results_nn_t res;
  uint8_t** population = malloc(sizeof(uint8_t *) * population_size);
  uint8_t** children = malloc(sizeof(uint8_t *) *
                              population_size * n_children);
  struct str* results = (struct str*) malloc(sizeof(struct str) *
                                             population_size * n_children);

  for (int i = 0; i < n_simulations; ++i) {
    /* Initialize rule */
    if (i == 0 && input_flag == 0) {
      generate_general_rule(grule_size, rule_array, rule_buf,
                            opts->states, opts->horizon);
    }
    /* Initialize search */
    if (i == 0) {
      asprintf(&genealogy_fname, "data_2d_%i/nn/%lu.gen", opts->states,
               hash(rule_buf));
      genealogy_file = fopen(genealogy_fname, "w+");

      for (int k = 0; k < population_size; ++k) {
        /* Initialize a population from the base rule */
        population[k] = (uint8_t*) malloc(sizeof(uint8_t) * grule_size);
        memcpy(population[k], rule_array, sizeof(uint8_t) * grule_size);

        perturb_rule(grule_size, population[k], rule_buf, opts->states,
                      opts->horizon, RATE);

        /* Allocate space for children rules */
        for (int d = 0; d < n_children; ++d) {
          children[k * n_children + d] =
            (uint8_t *) malloc(sizeof(uint8_t) * grule_size);
        }
      }
    }

    for (int k = 0; k < population_size; ++k) {
      for (int d = 0; d < n_children; ++d) {
        memcpy(children[k * n_children + d], population[k],
               sizeof(uint8_t) * grule_size);
        perturb_rule(grule_size, children[k * n_children + d],
                     rule_buf, opts->states,
                     opts->horizon, RATE);

        make_map(opts, rule_buf, i);

        res.nn_tr_5 = 0;
        res.nn_tr_50 = 0;
        res.nn_tr_300 = 0;

        process_rule(grule_size, children[k * n_children + d],
                     rule_buf, 0, timesteps, opts, &res);

        results[k * n_children + d].index = k * n_children + d;

        /* Either premature stop or some training did go to 0 */
        if (res.nn_tr_300 * res.nn_tr_50 * res.nn_tr_5 == 0) {
          results[k * n_children + d].value = 0.;
        }
        /* Everything went well */
        else {
          results[k * n_children + d].value =
            ((res.nn_tr_5/res.nn_tr_300 > 1) ? 1: 0)
            * ((1./3.) * res.nn_te_300/res.nn_tr_300
               + (1./8.) * res.nn_te_50/res.nn_tr_50
               + (13./24.) * res.nn_te_5/res.nn_tr_5);
        }
      }
    }

    qsort(results, n_children * population_size, sizeof(results[0]), cmp);
    for (int k = 0; k < population_size; ++k) {
      memcpy(population[k],
             children[results[n_children * population_size - 1 - k].index],
             sizeof(uint8_t) * grule_size);

      populate_buf(grule_size, population[k], rule_buf);
      fprintf(genealogy_file, "%lu\t", hash(rule_buf));
    }
    fprintf(genealogy_file, "\n");
    fflush(genealogy_file);
  }

  /* Free data structures */
  for (int k = 0; k < population_size; ++k) {
    free(population[k]);
    for (int d = 0; d < n_children; ++d) {
      free(children[k * n_children + d]);
    }
  }
  fclose(genealogy_file);
  free(genealogy_fname);
  free(population);
  free(children);
}


int main_2d(int argc, char** argv)
{
  char usage[] = "%s 2d [-n n_states] [-i input_rule] [-s size] [-t timesteps]"
    "[-g grain] [-c compress]";
  char one_input[] = "Give only one input, either -i rule or -f rule_file";


  extern char *optarg;
  extern int optind;
  int c, err = 0, input_flag = 0, search = 0;
  int timesteps = 1000, n_simulations = 1000;
  int compress_flag = 1;
  char* input_rule;
  char* input_fname = NULL;
  struct Options2D opts;
  opts.size = 256;
  opts.states = 2;
  opts.grain = 100;
  opts.horizon = 1;
  opts.grain_write = 100;
  opts.save_flag = STEP_FILE;
  opts.joint_complexity = 1;
  opts.early = EARLY;

  /* Optional arguments processing */
  while ((c = getopt(argc - 1, &argv[1], "n:i:s:t:g:c:z:f:mw:er")) != -1)
    switch (c) {
    case 'r':
      search = 1;
      break;
    case 'n':
      opts.states = atoi(optarg);
      break;
    case 'i':
    case 'f':
      if (input_flag == 1) {
        fprintf(stderr, one_input, NULL);
        exit(1);
      }
      input_flag = 1;
      if (c == 'i') {
        input_rule = (char*) calloc(strlen(optarg), sizeof(char));
        strcpy(input_rule, optarg);
      }
      else {
        input_fname = (char*) calloc(strlen(optarg), sizeof(char));
        strcpy(input_fname, optarg);
      }
      break;
    case 's':
      opts.size = atol(optarg);
      break;
    case 't':
      timesteps = atoi(optarg);
      break;
    case 'g':
      opts.grain = atoi(optarg);
      break;
    case 'c':
      compress_flag = 0;
      break;
    case 'z':
      n_simulations = atoi(optarg);
      break;
    case 'm':
      opts.save_flag = TMP_FILE;
      break;
    case 'w':
      opts.grain_write = atoi(optarg);
      break;
    case 'e':
      opts.early = NO_STOP;
      break;
    case '?':
      err = 1;
      break;
    }

  if (err) {
    fprintf(stderr, usage, argv[0]);
    exit(1);
  }

  time_t t;
  srand((unsigned)time(&t));

  const int side = 2 * opts.horizon + 1; /* Sidelength of neighborhood */
  const int neigh_size = side * side - 1; /* Total number of neighbors */
  /* Size of rule */
  const uint64_t grule_size = (int) pow(opts.states, neigh_size + 1);

  /* These arrays can be very big and are therefore allocated on the heap */
  char* rule_buf = malloc(grule_size * sizeof(char));
  uint8_t* rule_array = malloc(grule_size * sizeof(uint8_t));


  /* If input was given, it was either directly or via a file */
  if (input_fname) {
    FILE* rule_file;
    if ((rule_file = fopen(input_fname, "r"))) {
      uint64_t count = 0;
      while ((c = getc(rule_file)) != EOF && count < grule_size) {
        rule_array[count] = (uint8_t)(c - '0');
        rule_buf[count] = c;
        count++;
      }
      rule_buf[count] = '\0';
      if (count != grule_size) {
        fprintf(stderr, "Incorrect rule in file %s", input_fname);
        exit(1);
      }
    }
    else {
      fprintf(stderr, "Error while opening file %s", input_fname);
      exit(1);
    }
    free(input_fname);
  }
  /* Input given inline */
  else if (input_flag == 1) {
    build_rule_from_args(grule_size, rule_array, rule_buf, input_rule,
                         opts.states);
    free(input_rule);
  }


  /* If input rule was provided, and not starting a search: write steps for a
     given rule  */
  if (input_flag == 1 && search == 0) {

    results_nn_t res;
    /* This should not be necessary if the provided rule is already symmetric */
    /* TODO: Add possibility to work with either type */
    symmetrize_rule(grule_size, rule_array, opts.states, opts.horizon);

    make_map(&opts, rule_buf, 0);

    opts.save_steps = 1;
    process_rule(grule_size, rule_array, rule_buf, 0, timesteps, &opts, &res);
    free(rule_array);
    free(rule_buf);
    return 0;
  }

  /* The user wants to do a search */
  /* Iterative search */
  if (search == 1) {
    iterative_search(n_simulations, input_flag, timesteps, grule_size,
                     rule_array, rule_buf, &opts);
  }
  /* Generate compression plots for many rules */
  /* Regular search */
  else {
    for (int i = 0; i < n_simulations; ++i) {
      generate_general_rule(grule_size, rule_array, rule_buf,
                            opts.states, opts.horizon);

      make_map(&opts, rule_buf, i);

      results_nn_t res;
      opts.save_steps = 0;
      process_rule(grule_size, rule_array, rule_buf, 0, timesteps, &opts, &res);
    }
  }

  printf("\n");
  free(rule_array);
  free(rule_buf);
  return 0;
}

int main_1d(int argc, char** argv)
{
  extern char *optarg;
  extern int optind;
  int c, err = 0;
  size_t size = 256;
  int states = 2;
  struct Options1D options;
  options.init = ONE;
  options.timesteps = 256;
  options.grain = 50;
  options.write = NO_WRITE;
  options.radius = 1;

  char usage[] = "%s 1d [-s size] [-t timesteps]"
                 "[-n n_states] [-w neighborhood width]"
                 "[-i (one|random|random_small)] [-o]";
  char invalid_init[] = "Invalid value \"%s\" for init option."
                        " Must be one of \"one\", \"random\","
                        " \"random_small\"\n";

  while ((c = getopt(argc - 1, &argv[1], "s:t:n:r:i:og:")) != -1)
    switch (c) {
    case 's':
      size = atol(optarg);
      break;
    case 't':
      options.timesteps = atoi(optarg);
      break;
    case 'n':
      states = atoi(optarg);
      break;
    case 'r':
      options.radius = atoi(optarg);
      break;
    case 'o':
      options.write = WRITE_STEP;
      break;
    case 'i':
      if (strcmp("one", optarg) == 0) {
        options.init = ONE;
      }
      else if (strcmp("random", optarg) == 0) {
        options.init = RANDOM;
      }
      else if (strcmp("random_small", optarg) == 0) {
        options.init = RAND_SMALL;
      }
      else {
        printf(invalid_init, optarg);
        exit(1);
      }
      break;
    case 'g':
      options.grain = atoi(optarg);
      break;
    case '?':
      err = 1;
      break;
    }

  if (err) {
    fprintf(stderr, usage, argv[0]);
    exit(1);
  }

  size_t rule_size = (int)pow(states, 2 * options.radius + 1);
  uint8_t* rule = (uint8_t *)malloc(sizeof(uint8_t) * rule_size);

  time_t t;
  srand((unsigned)time(&t));

  uint64_t maxi = ((uint64_t)ipow(states, rule_size) > 300) ? 300:
    (uint64_t)pow(states, rule_size);

  for (uint64_t n = 0; n < maxi; n++) {
    for (size_t i = 0; i < rule_size; ++i) {
      if (maxi == 300) {
        rule[i] = (uint8_t)(rand() % states);
      }
      else {
        rule[i] = (uint8_t)((n / ipow(states, i)) % states);
      }
    }
    printf("%lu\t", rule_number(states, rule_size, rule));
    write_to_file(size, rule_size, rule, 0, &options, states);
    printf("\n");
  }

  free(rule);
  return 0;
}

int main(int argc, char** argv)
{
  char base_usage[] = "%s [\"1d\" or \"2d\"]";
  /* No first argument was passed */
  if (argc < 2) {
    printf("%s\n", help);
    return 0;
  }

  if (strcmp("2d", argv[1]) == 0) {
    return main_2d(argc, argv);
  }
  else if (strcmp("1d", argv[1]) == 0) {
    return main_1d(argc, argv);
  }
  else {
    printf("%s\n", help);
    fprintf(stderr, base_usage, argv[0]);
  }
}