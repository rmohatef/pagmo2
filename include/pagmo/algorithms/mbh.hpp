#ifndef PAGMO_ALGORITHMS_MBH_HPP
#define PAGMO_ALGORITHMS_MBH_HPP

#include <iomanip>
#include <random>
#include <string>
#include <tuple>

#include "../algorithm.hpp"
#include "../exceptions.hpp"
#include "../io.hpp"
#include "../population.hpp"
#include "../rng.hpp"
#include "../utils/constrained.hpp"

namespace pagmo
{

/// Monotonic Basin Hopping (generalized)
/**
 * \image html mbh.png "A schematic diagram illustrating the fitness landscape as seen by basin hopping" width=3cm
 *
 * Monotonic basin hopping, or simply, basin hopping, is an algorithm rooted in the idea of mapping
 * the objective function \f$f(\mathbf x_0)\f$ into the local minima found starting from \f$\mathbf x_0\f$.
 * This simple idea allows a substantial increase of efficiency in solving problems, such as the Lennard-Jones
 * cluster or the MGA-1DSM interplanetary trajectory problem that are conjectured to have a so-called
 * funnel structure.
 *
 * In pagmo we provide an original generalization of this concept resulting in a meta-algorithm that operates
 * on any pagmo::population using any suitable pagmo::algorithm. When a population containing a single
 * individual is used and coupled with a local optimizer, the original method is recovered.
 * The pseudo code of our generalized version is:
 * @code
 * > Select a pagmo::population
 * > Select a pagmo::algorithm
 * > Store best individual
 * > while i < stop_criteria
 * > > Perturb the population in a selected neighbourhood
 * > > Evolve the population using the algorithm
 * > > if the best individual is improved
 * > > > increment i
 * > > > update best individual
 * > > else
 * > > > i = 0
 * @endcode
 *
 * @see http://arxiv.org/pdf/cond-mat/9803344 for the paper inroducing the basin hopping idea for a Lennard-Jones
 * cluster optimization
 */
class mbh : public algorithm
{
public:
    /// Single entry of the log (feval, best fitness, n. constraints violated, violation norm, trial)
    typedef std::tuple<unsigned long long, double, vector_double::size_type, double, unsigned int> log_line_type;
    /// The log
    typedef std::vector<log_line_type> log_type;
    /// Default constructor, only here as serialization requires it
    mbh() : algorithm(compass_search{}), m_stop(5u), m_perturb(1, 1e-2), m_e(0u), m_seed(0u), m_verbosity(0u), m_log()
    {
    }
    /// Constructor
    template <typename T>
    explicit mbh(T &&a, unsigned int stop, double perturb, unsigned int seed = pagmo::random_device::next())
        : algorithm(std::forward<T>(a)), m_stop(stop), m_perturb(1, perturb), m_e(seed), m_seed(seed), m_verbosity(0u), m_log()
    {
    }
    /// Algorithm evolve method (juice implementation of the algorithm)
    population evolve(population pop) const
    {
        // We store some useful variables
        const auto &prob = pop.get_problem(); // This is a const reference, so using set_seed for example will not be
                                              // allowed (pop.set_problem_seed is)
        auto dim = prob.get_nx();             // This getter does not return a const reference but a copy
        auto nec = prob.get_nec();            // This getter does not return a const reference but a copy
        const auto bounds = prob.get_bounds();
        const auto &lb = bounds.first;
        const auto &ub = bounds.second;
        auto NP = pop.size();

        auto fevals0 = prob.get_fevals(); // discount for the already made fevals
        unsigned int count = 1u;          // regulates the screen output

        // PREAMBLE-------------------------------------------------------------------------------------------------
        if (prob.get_nobj() != 1u) {
            pagmo_throw(std::invalid_argument, "Multiple objectives detected in " + prob.get_name() + " instance. "
                                                   + get_name() + " cannot deal with them");
        }
        // Get out if there is nothing to do.
        if (m_stop == 0u) {
            return pop;
        }
        // Check if the perturbation vector has size 1, in which case the whole perturbation vector is filled with
        if (m_perturb.size() == 1u) {
            for (decltype(dim) i = 1u; i < dim; ++i) {
                m_perturb.push_back(m_perturb[0]);
            }
        }
        // Check that the perturbation vector size equals the size of the problem
        if (m_perturb.size() != dim) {
            pagmo_throw(std::invalid_argument, "The perturbation vector size is: " + std::to_string(m_perturb.size())
                                                   + ", while the problem dimension is: " + std::to_string(dim)
                                                   + ". They need to be equal for MBH to work.");
        }
        // ---------------------------------------------------------------------------------------------------------

        // No throws, all valid: we clear the logs
        m_log.clear();
        // mbh main loop
        unsigned int i = 0u;
        while (i < m_stop) {
            // 1 - We make a copy of the current population
            population pop_old(pop);
            // 2 - We perturb the current population (NP funevals are made here)
            for (decltype(NP) j = 0u; j < NP; ++j) {
                vector_double tmp_x(dim);
                for (decltype(dim) k = 0u; k < dim; ++k) {
                    tmp_x[k] = std::uniform_real_distribution<double>(
                        std::max(pop.get_x()[j][k] - m_perturb[k] * (ub[k] - lb[k]), lb[k]),
                        std::min(pop.get_x()[j][k] + m_perturb[k] * (ub[k] - lb[k]), ub[k]))(m_e);
                }
                pop.set_x(j, tmp_x);
            }
            // 3 - We evolve the current population with the selected algorithm
            pop = static_cast<const algorithm *>(this)->evolve(pop);
            i++;
            if (m_verbosity > 0u) {
                std::cout << i << ". "
                          << "\tLocal solution: " << pop.get_f()[pop.best_idx()][0]
                          << "\tGlobal best: " << pop_old.get_f()[pop_old.best_idx()][0];
                if (!prob.feasibility_f(pop.get_f()[pop.best_idx()])) {
                    std::cout << " i";
                }
                std::cout << std::endl; // we flush here as we want the user to read in real time ...
            }
            // 4 - We reset the counter if we have improved, otherwise we reset the population
            if (compare_fc(pop.get_f()[pop.best_idx()], pop_old.get_f()[pop_old.best_idx()], nec, prob.get_c_tol())) {
                i = 0u;
            } else {
                for (decltype(NP) j = 0u; j < NP; ++j) {
                    pop.set_xf(j, pop_old.get_x()[j], pop_old.get_f()[j]);
                }
            }
        }
        // We extract chromosomes and fitnesses
        return pop;
    }
    /// Sets the algorithm seed
    void set_seed(unsigned int seed)
    {
        m_seed = seed;
    }
    /// Gets the seed
    unsigned int get_seed() const
    {
        return m_seed;
    }
    /// Sets the verbosity level
    void set_verbosity(unsigned int level)
    {
        m_verbosity = level;
    };
    /// Gets the verbosity level
    unsigned int get_verbosity() const
    {
        return m_verbosity;
    }
    /// Algorithm name
    std::string get_name() const
    {
        return "Monotonic Basin Hopping (MBH) - Generalized";
    }
    /// Extra informations
    std::string get_extra_info() const
    {
        std::ostringstream ss;
        stream(ss, "\tStop: ", m_stop);
        stream(ss, "\n\tPerturbation vector: ", m_perturb);
        stream(ss, "\n\tSeed: ", m_seed);
        stream(ss, "\n\tVerbosity: ", m_verbosity);
        stream(ss, "\n\n\tInner algorithm: ", static_cast<const algorithm *>(this)->get_name());
        stream(ss, "\n\tInner algorithm extra info: ");
        stream(ss, "\n", static_cast<const algorithm *>(this)->get_extra_info());
        return ss.str();
    }
    /// Serialization
    template <typename Archive>
    void serialize(Archive &ar)
    {
        ar(cereal::base_class<algorithm>(this), m_stop, m_perturb, m_e, m_seed, m_verbosity, m_log);
    }

private:
    // Delete all that we do not want to inherit from problem
    // A - Common to all meta
    template <typename T>
    bool is() const = delete;
    bool has_set_seed() const = delete;
    bool is_stochastic() const = delete;
    bool has_set_verbosity() const = delete;
    // NOTE: We delete the streaming operator overload called with mbh, otherwise the inner algo would stream
    // NOTE: If a streaming operator is wanted for this class remove the line below and implement it
    friend std::ostream &operator<<(std::ostream &, const mbh &) = delete;

    unsigned int m_stop;
    // The member m_perturb is mutable as to allow to construct mbh also using a perturbation defined as a scalar
    // (in which case upon the first call to evolve it is expanded to the problem dimension)
    // and as a vector (in which case mbh will only operate on problem having the correct dimension)
    // While the use of "mutable" is not encouraged, in this case the alternative would be to have the user
    // construct the mbh algo passing one further parameter (the problem dmension) rather than having this determined
    // upon the first call to evolve.
    mutable std::vector<double> m_perturb;
    mutable detail::random_engine_type m_e;
    unsigned int m_seed;
    unsigned int m_verbosity;
    mutable log_type m_log;
};
}
PAGMO_REGISTER_ALGORITHM(pagmo::mbh)

#endif
