#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A predictor counter stores values from 0 to 7. Values 0-3 mean predict not-taken, and values 4-7 mean predict taken
 */
static int predict_counter(int c)
{
    return c >= 4;
}

/*
 * Update one 3-bit counter after the actual branch result is known. If the actual result is taken then counter++, otherwise counter--. The value remains in the range 0 to 7
 */
static void update_counter(int *c, int branch_taken)
{
    if (branch_taken) {
        if (*c < 7) {
            (*c)++;
        }
    } else {
        if (*c > 0) {
            (*c)--;
        }
    }
}

/*
 * Compute the table index for a bimodal predictor. The lowest two PC bits are ignored as they are 0
 *  the lowest m remaining bits select one entry from the 2^m-entry prediction table
 */
static unsigned int b_index(unsigned long long pc, int m)
{
    unsigned int mask = (1u << m) - 1u;
    return (unsigned int)((pc >> 2) & mask);
}

/*
 * Compute the chooser table index for the hybrid predictor
 * It uses the same PC-bit style as bimodal, except the number of index bits is K
 */
static unsigned int c_index(unsigned long long pc, int k)
{
    unsigned int mask = (1u << k) - 1u;
    return (unsigned int)((pc >> 2) & mask);
}

/*
 * Compute the gshare prediction table index. First it builds the normal m-bit PC index 
 * Then split that value conceptually into upper bits and lower n bits. The lower n bits are XORed with the global branch history register, and the upper bits stay unchanged
 */
static unsigned int g_index(unsigned long long pc, int m, int n, unsigned int h)
{
    unsigned int pc_index = b_index(pc, m);

    if (n == 0) {
        return pc_index;
    }

    unsigned int l_mask = (1u << n) - 1u;
    unsigned int l = pc_index & l_mask;
    unsigned int u = pc_index >> n;
    unsigned int xored = l ^ h;

    return (u << n) | xored;
}

/*
 * Update the global branch history register after each branch
 * It is shifted right by one bit and placed the newest actual outcome in the most significant history bit
 * The mask keeps only n useful history bits
 */
static unsigned int update_h(unsigned int h, int n, int branch_taken)
{
    if (n == 0) {
        return 0;
    }

    unsigned int mask = (1u << n) - 1u;
    h = h >> 1;
    if (branch_taken) {
        h = h | (1u << (n - 1));
    }
    return h & mask;
}

/*
 * Update the 2-bit hybrid chooser counter
 * If gshare was correct and bimodal was wrong, then gshare is chosen. If bimodal was correct and gshare was wrong, bimodal is chosen
 * If both were correct or both were wrong, the chooser is not changed.
 */
static void update_chooser(int *c, int g_correct, int b_correct)
{
    if (g_correct && !b_correct) {
        if (*c < 3) {
            (*c)++;
        }
    } else if (!g_correct && b_correct) {
        if (*c > 0) {
            (*c)--;
        }
    }
}

/*
 * Return only the filename portion of a trace path if the trace files are under any subfolder like traces/
 * This lets the simulator open paths such as traces/gcc_trace.txt while still printing gcc_trace.txt in the COMMAND line, which matches validation files
 */
static const char *trace_basename(const char *trace_file)
{
    const char *trace_name = strrchr(trace_file, '/');
    if (trace_name == NULL) {
        return trace_file;
    }
    return trace_name + 1;
}

/*
 * Run a full bimodal predictor simulation creating a 2^m prediction table, initializing all counters to 4
 * reading every branch from the trace, recording mispredictions, training the selected counter, and finally printing the exact report format required by the project
 */
static int run_b(int m, const char *trace_file)
{
    FILE *fp = fopen(trace_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: could not open trace file %s\n", trace_file);
        return 1;
    }

    unsigned int table_size = 1u << m;
    int *table = malloc(table_size * sizeof(int));
    if (table == NULL) {
        fclose(fp);
        fprintf(stderr, "error: could not allocate predictor table\n");
        return 1;
    }

    /*
     * Every bimodal table entry starts as weakly taken and its presented by 4
     */
    for (unsigned int i = 0; i < table_size; i++) {
        table[i] = 4;
    }

    unsigned long long pc;
    char o;
    unsigned long long p = 0;
    unsigned long long misp = 0;

    while (fscanf(fp, "%llx %c", &pc, &o) == 2) {
        int branch_taken;

        if (o == 't') {
            branch_taken = 1;
        } else if (o == 'n') {
            branch_taken = 0;
        } else {
            fprintf(stderr, "error: invalid outcome '%c' in trace file\n", o);
            free(table);
            fclose(fp);
            return 1;
        }

        /*
         * Use the PC to choose one counter, make the prediction from that counter, compare it with the trace outcome, and then train the counter
         */
        unsigned int index = b_index(pc, m);
        int predicted_taken = predict_counter(table[index]);

        if (predicted_taken != branch_taken) {
            misp++;
        }

        update_counter(&table[index], branch_taken);
        p++;
    }

    const char *trace_name = trace_basename(trace_file);

    printf("COMMAND\n");
    printf("./sim bimodal %d %s\n", m, trace_name);
    printf("OUTPUT\n");
    printf("number of predictions:\t\t%llu\n", p);
    printf("number of mispredictions:\t%llu\n", misp);
    printf("misprediction rate:\t\t%.2f%%\n",
           p == 0 ? 0.0 : (100.0 * misp) / p);
    printf("FINAL BIMODAL CONTENTS\n");

    for (unsigned int i = 0; i < table_size; i++) {
        printf("%u\t%d\n", i, table[i]);
    }

    free(table);
    fclose(fp);
    return 0;
}

/*
 * Run a full gshare predictor simulation. similar to bimodal, but it keeps a global history register
 * The current history participates in the table index, and after each branch the history is updated with the actual branch result
 */
static int run_g(int m, int n, const char *trace_file)
{
    FILE *fp = fopen(trace_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: could not open trace file %s\n", trace_file);
        return 1;
    }

    unsigned int table_size = 1u << m;
    int *table = malloc(table_size * sizeof(int));
    if (table == NULL) {
        fclose(fp);
        fprintf(stderr, "error: could not allocate predictor table\n");
        return 1;
    }

    /*
     * Gshare uses the same kind of 3-bit counters as bimodal with initial counter values of 4
     */
    for (unsigned int i = 0; i < table_size; i++) {
        table[i] = 4;
    }

    /*
     * The global history register starts with all zero bits, meaning the simulator has not seen any previous branch outcomes
     */
    unsigned int h = 0;
    unsigned long long pc;
    char o;
    unsigned long long p = 0;
    unsigned long long misp = 0;

    while (fscanf(fp, "%llx %c", &pc, &o) == 2) {
        int branch_taken;

        if (o == 't') {
            branch_taken = 1;
        } else if (o == 'n') {
            branch_taken = 0;
        } else {
            fprintf(stderr, "error: invalid outcome '%c' in trace file\n", o);
            free(table);
            fclose(fp);
            return 1;
        }

        /*
         * The gshare index depends on both the current PC and the current history.The counter is trained first, and then the history register is updated
         */
        unsigned int index = g_index(pc, m, n, h);
        int predicted_taken = predict_counter(table[index]);

        if (predicted_taken != branch_taken) {
            misp++;
        }

        update_counter(&table[index], branch_taken);
        h = update_h(h, n, branch_taken);
        p++;
    }

    printf("COMMAND\n");
    printf("./sim gshare %d %d %s\n", m, n, trace_basename(trace_file));
    printf("OUTPUT\n");
    printf("number of predictions:\t\t%llu\n", p);
    printf("number of mispredictions:\t%llu\n", misp);
    printf("misprediction rate:\t\t%.2f%%\n",
           p == 0 ? 0.0 : (100.0 * misp) / p);
    printf("FINAL GSHARE CONTENTS\n");

    for (unsigned int i = 0; i < table_size; i++) {
        printf("%u\t%d\n", i, table[i]);
    }

    free(table);
    fclose(fp);
    return 0;
}

/*
 * Run a full hybrid predictor simulation.
 * Hybrid has three tables: a chooser table, a gshare prediction table, and a bimodal prediction table. Both predictors make a prediction
 * the chooser decides which prediction to trust, and only the selected predictor is trained
 */
static int run_hybrid(int k, int m1, int n, int m2, const char *trace_file)
{
    FILE *fp = fopen(trace_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: could not open trace file %s\n", trace_file);
        return 1;
    }

    unsigned int c_size = 1u << k;
    unsigned int g_size = 1u << m1;
    unsigned int b_size = 1u << m2;

    int *chooser = malloc(c_size * sizeof(int));
    int *g_table = malloc(g_size * sizeof(int));
    int *b_table = malloc(b_size * sizeof(int));
    if (chooser == NULL || g_table == NULL || b_table == NULL) {
        free(chooser);
        free(g_table);
        free(b_table);
        fclose(fp);
        fprintf(stderr, "error: could not allocate predictor tables\n");
        return 1;
    }

    /*
     * The chooser table starts at 1, which weakly selects bimodal because chooser values 0 and 1 choose bimodal, while 2 and 3 choose gshare
     * Both prediction tables start with counters initialized to 4
     */
    for (unsigned int i = 0; i < c_size; i++) {
        chooser[i] = 1;
    }
    for (unsigned int i = 0; i < g_size; i++) {
        g_table[i] = 4;
    }
    for (unsigned int i = 0; i < b_size; i++) {
        b_table[i] = 4;
    }

    unsigned int h = 0;
    unsigned long long pc;
    char o;
    unsigned long long p = 0;
    unsigned long long misp = 0;

    while (fscanf(fp, "%llx %c", &pc, &o) == 2) {
        int branch_taken;

        if (o == 't') {
            branch_taken = 1;
        } else if (o == 'n') {
            branch_taken = 0;
        } else {
            fprintf(stderr, "error: invalid outcome '%c' in trace file\n", o);
            free(chooser);
            free(g_table);
            free(b_table);
            fclose(fp);
            return 1;
        }

        /*
         * For every branch, compute all three indexes and get both predictions. The chooser counter decides whether the final prediction comes from gshare or from bimodal
         */
        unsigned int g_i = g_index(pc, m1, n, h);
        unsigned int b_i = b_index(pc, m2);
        unsigned int c_i = c_index(pc, k);

        int g_pred = predict_counter(g_table[g_i]);
        int b_pred = predict_counter(b_table[b_i]);
        int use_g = chooser[c_i] >= 2;
        int final_pred = use_g ? g_pred : b_pred;

        /*
         * Count a misprediction using only the final selected prediction
         * train only the selected predictor table
         */
        if (final_pred != branch_taken) {
            misp++;
        }

        if (use_g) {
            update_counter(&g_table[g_i], branch_taken);
        } else {
            update_counter(&b_table[b_i], branch_taken);
        }

        update_chooser(&chooser[c_i],
                       g_pred == branch_taken,
                       b_pred == branch_taken);
        /*
         * The chooser is trained using which predictor was correct
         * Gshare history is always updated, even if the chooser selected bimodal
         */
        h = update_h(h, n, branch_taken);
        p++;
    }

    printf("COMMAND\n");
    printf("./sim hybrid %d %d %d %d %s\n", k, m1, n, m2, trace_basename(trace_file));
    printf("OUTPUT\n");
    printf("number of predictions:\t\t%llu\n", p);
    printf("number of mispredictions:\t%llu\n", misp);
    printf("misprediction rate:\t\t%.2f%%\n",
           p == 0 ? 0.0 : (100.0 * misp) / p);
    printf("FINAL CHOOSER CONTENTS\n");
    for (unsigned int i = 0; i < c_size; i++) {
        printf("%u\t%d\n", i, chooser[i]);
    }
    printf("FINAL GSHARE CONTENTS\n");
    for (unsigned int i = 0; i < g_size; i++) {
        printf("%u\t%d\n", i, g_table[i]);
    }
    printf("FINAL BIMODAL CONTENTS\n");
    for (unsigned int i = 0; i < b_size; i++) {
        printf("%u\t%d\n", i, b_table[i]);
    }

    free(chooser);
    free(g_table);
    free(b_table);
    fclose(fp);
    return 0;
}

/*
 * Program entry point
 * This checks which predictor type was requested, validates the number and range of command-line arguments, and then calls the matching simulator routine
 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s bimodal <M2> <tracefile>\n", argv[0]);
        fprintf(stderr, "usage: %s gshare <M1> <N> <tracefile>\n", argv[0]);
        fprintf(stderr, "usage: %s hybrid <K> <M1> <N> <M2> <tracefile>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "bimodal") == 0) {
        if (argc != 4) {
            fprintf(stderr, "usage: %s bimodal <M2> <tracefile>\n", argv[0]);
            return 1;
        }

        int m = atoi(argv[2]);
        if (m <= 0 || m >= 31) {
            fprintf(stderr, "error: M2 must be between 1 and 30\n");
            return 1;
        }

        return run_b(m, argv[3]);
    } else if (strcmp(argv[1], "gshare") == 0) {
        if (argc != 5) {
            fprintf(stderr, "usage: %s gshare <M1> <N> <tracefile>\n", argv[0]);
            return 1;
        }

        int m = atoi(argv[2]);
        int n = atoi(argv[3]);

        if (m <= 0 || m >= 31) {
            fprintf(stderr, "error: M1 must be between 1 and 30\n");
            return 1;
        }
        if (n < 0 || n > m) {
            fprintf(stderr, "error: N must be between 0 and M1\n");
            return 1;
        }

        return run_g(m, n, argv[4]);
    } else if (strcmp(argv[1], "hybrid") == 0) {
        if (argc != 7) {
            fprintf(stderr, "usage: %s hybrid <K> <M1> <N> <M2> <tracefile>\n", argv[0]);
            return 1;
        }

        int k = atoi(argv[2]);
        int m1 = atoi(argv[3]);
        int n = atoi(argv[4]);
        int m2 = atoi(argv[5]);

        if (k <= 0 || k >= 31) {
            fprintf(stderr, "error: K must be between 1 and 30\n");
            return 1;
        }
        if (m1 <= 0 || m1 >= 31) {
            fprintf(stderr, "error: M1 must be between 1 and 30\n");
            return 1;
        }
        if (m2 <= 0 || m2 >= 31) {
            fprintf(stderr, "error: M2 must be between 1 and 30\n");
            return 1;
        }
        if (n < 0 || n > m1) {
            fprintf(stderr, "error: N must be between 0 and M1\n");
            return 1;
        }

        return run_hybrid(k, m1, n, m2, argv[6]);
    }

    fprintf(stderr, "error: unknown predictor type %s\n", argv[1]);
    return 1;
}
