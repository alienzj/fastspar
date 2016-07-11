#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <vector>

#include <armadillo>
#include <gsl/gsl_cdf.h>
#include <getopt.h>
#include <glob.h>


extern "C" {
    void c_gausq2(int* n, double* d, double* e, double* z, int* ierr);
}


struct OtuTable {
    std::vector<std::string> sample_names;
    std::vector<std::string> otu_ids;
    std::vector<int> otu_observations;
    int otu_number;
    int sample_number;
};


struct OtuTable loadOtuFile(std::string filename) {
    // Used to store strings from file prior to assignment
    std::string line;
    std::string ele;
    std::stringstream line_stream;
    // Other variables
    struct OtuTable otu_table;
    int otu_number = 0;
    int sample_number = 0;
    bool id;
    // Open file stream
    std::ifstream otu_file;
    otu_file.open(filename);
    // Process header
    std::getline(otu_file, line);
    line_stream.str(line);
    // Iterate header columns
    while(std::getline(line_stream, ele, '\t')) {
        //TODO: Add assertion here
        // Skip the OTU_id column (first column)
        if (ele == "OTU_id" || ele == "#OTU ID") {
            continue;
        }
        // Store samples
        otu_table.sample_names.push_back(ele);
        ++sample_number;
    }
    // Process sample counts, need to get OTU IDS first
    while(std::getline(otu_file, line)) {
        // TODO: Is there an alternate design pattern to loop variables as below
        // Loop variables
        // (Re)sets variables for loop
        id = true;
        line_stream.clear();
        // Add current line to line stream and then split by tabs
        line_stream.str(line);
        while (std::getline(line_stream, ele, '\t')) {
            // Grab the OTU_id
            if (id) {
                otu_table.otu_ids.push_back(ele);
                id = false;
                continue;
            }
            // Add current element to OTU count after converting to int
            otu_table.otu_observations.push_back(std::stoi(ele));
        }
        ++otu_number;
    // TODO: Check if growing std::vector is sustainable for large tables
    }
    // Add counts to otu_table struct
    otu_table.otu_number = otu_number;
    otu_table.sample_number = sample_number;
    return otu_table;
}


std::vector<std::string> getBootstrapCorrelationPaths(std::string& glob_path) {
    // Perform glob
    glob_t glob_results;
    glob(glob_path.c_str(), GLOB_TILDE, NULL, &glob_results);

    // Put globbed paths onto string vector
    std::vector<std::string> bootstrap_correlations;
    for(unsigned int i = 0; i < glob_results.gl_pathc; ++i) {
        bootstrap_correlations.push_back(std::string(glob_results.gl_pathv[i]));
    }

    // Free glob memory and return string vector
    globfree(&glob_results);
    return bootstrap_correlations;
}


arma::Mat<double> loadCorrelation(std::string& filename, struct OtuTable& otu_table) {
    // Used to store strings from file prior to matrix construction
    std::string line;
    std::string ele;
    std::stringstream line_stream;
    std::vector<double> correlations_vector;
    correlations_vector.reserve(otu_table.otu_number * otu_table.otu_number);
    // Other variables
    bool id;
    // Open file stream
    std::ifstream cor_file;
    cor_file.open(filename);
    // Skip header, order SHOULD be the same as input OTU table
    std::getline(cor_file, line);
    line_stream.str(line);
    // Process correlation elements
    while(std::getline(cor_file, line)) {
        // (Re)sets variables for loop
        id = true;
        line_stream.clear();
        // Add current line to line stream and then split by tabs
        line_stream.str(line);
        while (std::getline(line_stream, ele, '\t')) {
            // Skip the OTU ID column
            if (id) {
                id = false;
                continue;
            }
            // Add current element to correlation mat after converting to double
            correlations_vector.push_back(std::stod(ele));
        }
    }
    // Construct matrix and return it
    arma::Mat<double> correlations(correlations_vector);
    correlations.reshape(otu_table.otu_number, otu_table.otu_number);
    return correlations;
}


void countValuesMoreExtreme(arma::Mat<double>& abs_observed_correlation,
                            arma::Mat<double>& abs_bootstrap_correlation,
                            arma::Mat<int>& extreme_value_counts) {
    // Find values more extreme than observed in bootstrap for each i,j element
    arma::Col<arma::uword> extreme_value_index = arma::find(abs_bootstrap_correlation >= abs_observed_correlation);
    // For each more extreme value, increment count in extreme_value_couts
    for (arma::Col<arma::uword>::iterator it = extreme_value_index.begin(); it != extreme_value_index.end(); ++it) {
        extreme_value_counts(*it) += 1;
    }

}

void writeOutMatrix(arma::Mat<int>& matrix, std::string out_filename, struct OtuTable& otu_table) {
    // Get stream handle
    std::ofstream outfile;
    outfile.open(out_filename);
    // Write out header
    outfile << "#OTU ID";
    for (std::vector<std::string>::iterator it = otu_table.otu_ids.begin(); it != otu_table.otu_ids.end(); ++it) {
        outfile << "\t" << *it;
    }
    outfile << std::endl;
    // Write out values
    for (unsigned int i = 0; i < matrix.n_rows; ++i) {
        for (unsigned int j = 0; j < matrix.n_cols; ++j) {
            // Write the OTU id as first field in row
            if (j == 0) {
                outfile << otu_table.sample_names[i];
            }
            outfile << "\t" << matrix(i, j);
        }
        outfile << std::endl;
    }
}


void printHelp() {
    std::cerr << "Program: SparCpp exact p-values (c++ implementation of SparCC)" << std::endl;
    std::cerr << "Version: 0.1a" << std::endl;
    std::cerr << "Contact: Stephen Watts (s.watts2@student.unimelb.edu.au)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  exact_pavlues --otu_table <of> --correlation <rf> --prefix <p>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "  -t/--otu_table <of>   OTU input table used to generated correlations" << std::endl;
    std::cerr << "  -r/--correlation <rf> Correlation table generated by SparCpp" << std::endl;
    std::cerr << "  -p/--prefix <p>       Prefix output of bootstrap output files" << std::endl;

}


int main(int argc, char **argv) {
    // Define some variables
    std::string otu_filename;
    std::string correlation_filename;
    std::string bootstrap_prefix;

    // Commandline arguments (for getlongtops)
    struct option long_options[] =
        {
            {"otu_table", required_argument, NULL, 't'},
            {"prefix", required_argument, NULL, 'p'},
            {"correlation", required_argument, NULL, 'r'},
            {"help", no_argument, NULL, 'h'},
            {NULL, 0, 0, 0}
        };

    // Check if have an attemp at arguments, else print help
    if (argc < 2) {
        printHelp();
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: options -t/--otu_table, -r/--correlation, and -p/--prefix are required" << std::endl;
        exit(0);
    }

    // Parse commandline arguments
    while (1) {
        int option_index = 0;
        int c;
        c = getopt_long (argc, argv, "ht:p:r:", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch(c) {
            // TODO: do we need case(0)?
            case 't':
                otu_filename = optarg;
                break;
            case 'r':
                correlation_filename = optarg;
                break;
            case 'p':
                bootstrap_prefix = optarg;
                break;
            case 'h':
                printHelp();
                exit(0);
            default:
                exit(1);
        }
    }

    // Abort execution if given unknown arguments
    if (optind < argc){
        std::cerr << argv[0] << " invalid argument: " << argv[optind++] << std::endl;
    }
    // Make sure we have filenames and parameters
    if (otu_filename.empty()) {
        printHelp();
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: argument -t/--otu_table is required" << std::endl;
        exit(1);
    }
    if (bootstrap_prefix.empty()) {
        printHelp();
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: argument -p/--prefix is required" << std::endl;
        exit(1);
    }
    if (correlation_filename.empty()) {
        printHelp();
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: argument -r/--correlation is required" << std::endl;
        exit(1);
    }
    // Check that the OTU file exists
    std::ifstream checkfile;
    checkfile.open(otu_filename);
    if (!checkfile.good()) {
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: OTU table file " << otu_filename << " does not exist" << std::endl;
        exit(1);
    }
    checkfile.close();
    // Check that the OTU file exists
    checkfile.open(correlation_filename);
    if (!checkfile.good()) {
        std::cerr << std::endl << argv[0];
        std::cerr << ": error: correlation table file " << correlation_filename << " does not exist" << std::endl;
        exit(1);
    }
    checkfile.close();
    // Make sure our prefix has a trailing '*' for globbing
    if (bootstrap_prefix.back() != '*') {
        bootstrap_prefix += "*";
    }


    // Read in otu tables (used to calculate total possible permutations)
    struct OtuTable otu_table = loadOtuFile(otu_filename);
    arma::Mat<int> counts(otu_table.otu_observations);
    counts.reshape(otu_table.sample_number, otu_table.otu_number);

    // Read in observed correlation
    arma::Mat<double> observed_correlation = loadCorrelation(correlation_filename, otu_table);
    arma::Mat<double> abs_observed_correlation = arma::abs(observed_correlation);

    // For through vector of bootstrap correlations and count values for i, j elements that are more extreme than obs
    arma::Mat<int> extreme_value_counts(otu_table.otu_number, otu_table.otu_number, arma::fill::zeros);
    std::vector<std::string> bs_cor_paths = getBootstrapCorrelationPaths(bootstrap_prefix);
    for (std::vector<std::string>::iterator it = bs_cor_paths.begin(); it != bs_cor_paths.end(); ++it) {
        arma::Mat<double> bootstrap_correlation = loadCorrelation(*it, otu_table);
        arma::Mat<double> abs_bootstrap_correlation = arma::abs(bootstrap_correlation);
        countValuesMoreExtreme(abs_observed_correlation, abs_bootstrap_correlation, extreme_value_counts);
    }

    // Calculate total possible permutations  for each OTU (need to take into account floating error)
    std::unordered_map<int, int> count_frequency;
    for (arma::Mat<int>::col_iterator it = counts.begin_col(0); it != counts.end_col(0); ++it) {
        ++count_frequency[*it];
    }
    // Get factorial product (fac then multiple all together) of all but the most frequent
    //          freqs_not_highest <- freqs(!max(freqs))
    //          denominator <- Reduce('*', factorial(freqs_not_highest))
    // Multiple reduce the sequence from number of samples down to (highest frequency + 1)
    //          seq <- otu_table.sample_number:(highest_freq +1)
    //          numerator <- multiple_reduce(seq)
    //  Divide numerator by denominator. This is import as calculating factorials over ~170 is not
    //  feasible (but can be approximated)
    for (std::unordered_map<int, int>::iterator it = count_frequency.begin(); it != count_frequency.end(); ++it) {
        it->first;
        it->second;
    }
    //counts.col(0).print();


    // TEMP
    int possible_permutations = 10000;
    int permutations = 5000;
    int more_extreme_count = 2;

    // Start statmod::permp
    if (possible_permutations <= 10000 ) {
        // Exact p-value calculation
        double prob[possible_permutations];
        double prob_binom_sum;
        double pvalue;
        for (int i = 0; i < possible_permutations; ++i) {
            prob[i] = (double)(i + 1) / possible_permutations;
        }
        for (int i = 0; i < possible_permutations; ++i) {
            prob_binom_sum += gsl_cdf_binomial_P(more_extreme_count, prob[i], permutations);
        }
        pvalue = prob_binom_sum / possible_permutations;
    } else {
        // Integral approximation for p-value calculation
        // Start statmod::gaussquad
        // TODO: See if there is a better way to init array elements w/o hard coding
        int n = 128;
        double a[n];
        double b[n];
        double z[n];
        int ierr = 0;

        // Initialise array elements
        for (int i = 0; i < n; ++i) {
            a[i] = 0;
            z[i] = 0;
        }
        for (int i = 0; i < (n - 1); ++i){
            int i1 = i + 1;
            b[i] = i1 / sqrt(4 * pow(i1, 2) - 1);
        }
        b[n] = 0;
        z[0] = 1;

        // Make call to Fortran subroutine. Variables a, b, z and ierr are modified
        c_gausq2(&n, a, b, z, &ierr);

        // Further cals
        double u = 0.5 / possible_permutations;
        double weights[n];
        double nodes[n];
        for (int i = 0; i < n; ++i) {
            weights[i] = pow(z[i], 2);
        }
        for (int i = 0; i < n; ++i) {
            nodes[i] = u * (a[i] + 1) / 2;
        }
        // End statmod::gaussquad

        double weight_prob_product_sum;
        for (int i = 0; i < n; ++i) {
            weight_prob_product_sum += gsl_cdf_binomial_P(more_extreme_count, nodes[i], permutations) * weights[i];
        }
        double integral = 0.5 / (possible_permutations * weight_prob_product_sum);
        double pvalue = ((double)more_extreme_count + 1) / ((double)permutations + 1) - integral;
    }
    // End statmod::permp
}