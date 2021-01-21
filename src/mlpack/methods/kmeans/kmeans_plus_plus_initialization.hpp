/**
 * @file methods/kmeans/kmeans_plus_plus_initialization.hpp
 * @author Ryan Curtin
 *
 * This file implements the k-means++ initialization strategy.
 */
#ifndef KMEANS_PLUS_PLUS_INITIALIZATION_HPP
#define KMEANS_PLUS_PLUS_INITIALIZATION_HPP

#include <mlpack/prereqs.hpp>

/**
 * This class implements the k-means++ initialization, as described in the
 * following paper:
 *
 * @code
 * @inproceedings{arthur2007k,
 *   title={k-means++: The advantages of careful seeding},
 *   author={Arthur, David and Vassilvitskii, Sergei},
 *   booktitle={Proceedings of the Eighteenth Annual ACM-SIAM Symposium on
 *        Discrete Algorithms (SODA '07)},
 *   pages={1027--1035},
 *   year={2007},
 *   organization={Society for Industrial and Applied Mathematics}
 * }
 * @endcode
 *
 * In accordance with mlpack's InitialPartitionPolicy template type, we only
 * need to implement a constructor and a method to compute the initial
 * centroids.
 */
class KMeansPlusPlusInitialization
{
 public:
  //! Empty constructor, required by the InitialPartitionPolicy type definition.
  KMeansPlusPlusInitialization() { }

  /**
   * Initialize the centroids matrix by randomly sampling points from the data
   * matrix.
   *
   * @param data Dataset.
   * @param clusters Number of clusters.
   * @param centroids Matrix to put initial centroids into.
   */
  template<typename MatType>
  inline static void Cluster(const MatType& data,
                             const size_t clusters,
                             arma::mat& centroids)
  {
    centroids.set_size(data.n_rows, clusters);

    // We'll sample our first point fully randomly.
    size_t firstPoint = mlpack::math::RandInt(0, data.n_cols);
    centroids.col(0) = data.col(firstPoint);

    // Utility variable.
    arma::vec distribution(data.n_cols);

    // Now, sample other points...
    for (size_t i = 1; i < clusters; ++i)
    {
      // We must compute the CDF for sampling... this depends on the computation
      // of the minimum distance between each point and its closest
      // already-chosen centroid.
      //
      // This computation is ripe for speedup with trees!  I am not sure exactly
      // how much we would need to approximate, but I think it could be done
      // without breaking the O(log k)-competitive guarantee (I think).
      for (size_t p = 0; p < data.n_cols; ++p)
      {
        double minDistance = std::numeric_limits<double>::max();
        for (size_t j = 0; j < i; ++j)
        {
          const double distance =
              mlpack::metric::SquaredEuclideanDistance::Evaluate(data.col(p),
              centroids.col(j));
          minDistance = std::min(distance, minDistance);
        }

        distribution[p] = minDistance;
      }

      // Next normalize the distribution (actually technically we could avoid
      // this).
      distribution /= arma::accu(distribution);

      // Turn it into a CDF for convenience...
      for (size_t j = 1; j < distribution.n_elem; ++j)
        distribution[j] += distribution[j - 1];

      // Sample a point...
      const double sampleValue = mlpack::math::Random();
      double* elem = std::lower_bound(distribution.begin(), distribution.end(),
          sampleValue);
      size_t position = (size_t) (elem - distribution.begin()) / sizeof(double);
      centroids.col(i) = data.col(position);
    }
  }
};

#endif
