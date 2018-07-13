/**
 * @file lmnn_function_impl.cpp
 * @author Manish Kumar
 *
 * An implementation of the LMNNFunction class.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */
#ifndef MLPACK_METHODS_LMNN_FUNCTION_IMPL_HPP
#define MLPACK_METHODS_LMNN_FUNCTION_IMPL_HPP

#include "lmnn_function.hpp"

#include <mlpack/core/math/make_alias.hpp>
#include <mlpack/core/optimizers/function.hpp>

namespace mlpack {
namespace lmnn {

template<typename MetricType>
LMNNFunction<MetricType>::LMNNFunction(const arma::mat& dataset,
                                       const arma::Row<size_t>& labels,
                                       size_t k,
                                       double regularization,
                                       size_t range,
                                       MetricType metric) :
    dataset(math::MakeAlias(const_cast<arma::mat&>(dataset), false)),
    labels(math::MakeAlias(const_cast<arma::Row<size_t>&>(labels), false)),
    k(k),
    metric(metric),
    regularization(regularization),
    iteration(0),
    range(range),
    constraint(dataset, labels, k)
{
  // Initialize the initial learning point.
  initialPoint.eye(dataset.n_rows, dataset.n_rows);
  // Initialize transformed dataset to base dataset.
  transformedDataset = dataset;

  evalOld.set_size(k, k, dataset.n_cols);
  evalOld.fill(arma::datum::nan);

  maxImpNorm.set_size(k, dataset.n_cols);
  maxImpNorm.fill(0);

  // Initialize target neighbors & impostors.
  targetNeighbors = arma::Mat<size_t>(k, dataset.n_cols, arma::fill::zeros);
  impostors = arma::Mat<size_t>(k, dataset.n_cols, arma::fill::zeros);
  distance = arma::mat(k, dataset.n_cols, arma::fill::zeros);

  constraint.TargetNeighbors(targetNeighbors, dataset, labels);
  constraint.Impostors(impostors, dataset, labels);

  // Precalculate and save the gradient due to target neighbors.
  Precalculate();
}

//! Shuffle the dataset.
template<typename MetricType>
void LMNNFunction<MetricType>::Shuffle()
{
  arma::mat newDataset = dataset;
  arma::Mat<size_t> newLabels = labels;
  arma::cube newEvalOld = evalOld;
  arma::cube newTransformationOldPoint = transformationOldPoint;
  arma::mat newMaxImpNorm = maxImpNorm;

  // Generate ordering.
  arma::uvec ordering = arma::shuffle(arma::linspace<arma::uvec>(0,
      dataset.n_cols - 1, dataset.n_cols));

  math::ClearAlias(dataset);
  math::ClearAlias(labels);

  dataset = newDataset.cols(ordering);
  labels = newLabels.cols(ordering);
  maxImpNorm = newMaxImpNorm.cols(ordering);

  for (size_t i = 0; i < ordering.n_elem; i++)
  {
    evalOld.slice(i) = newEvalOld.slice(ordering(i));
    transformationOldPoint.slice(i) = newTransformationOldPoint.slice(ordering(i));
  }

  // Re-calculate target neighbors as indices changed.
  constraint.PreCalulated() = false;
  constraint.TargetNeighbors(targetNeighbors, dataset, labels);
}

//! Evaluate cost over whole dataset.
template<typename MetricType>
double LMNNFunction<MetricType>::Evaluate(const arma::mat& transformation)
{
  double cost = 0;

  // Apply metric over dataset.
  transformedDataset = transformation * dataset;

  // Calculate norm of change in transformation.
  double transformationDiff = 0;
  if (transformationOld.n_elem != 0)
  {
    transformationDiff = arma::norm(transformation - transformationOld);
  }

  if (iteration++ % range == 0)
  {
    // Re-calculate impostors on transformed dataset.
    constraint.Impostors(impostors, distance, transformedDataset, labels);
  }

  for (size_t i = 0; i < dataset.n_cols; i++)
  {
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate cost due to distance between target neighbors & data point.
      double eval = metric.Evaluate(transformedDataset.col(i),
                          transformedDataset.col(targetNeighbors(j, i)));
      cost += (1 - regularization) * eval;
    }

    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation. Here bp stands for
      // breaking point.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate cost due to {data point, target neighbors, impostors}
        // triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Bounds for eval.
        if (transformationOld.n_elem != 0 && !std::isnan(evalOld(l, j, i)))
        {
          // Update cache max impostor norm.
          maxImpNorm(l, i) = std::max(maxImpNorm(l, i), norm(impostors(l, i)));

          eval = evalOld(l, j, i) + transformationDiff *
            (norm(targetNeighbors(j, i)) + maxImpNorm(l, i) +
            2 * norm(i));

          // Check if there is need to calculate exact eval value.
          if (eval <= -1)
          {
            exactEval = false;
          }
          else
          {
            // Reset cacche max impostor norm.
            maxImpNorm(l, i) = 0;
            evalOld(l, j, i) = arma::datum::nan;
          }
        }

        // Calculate exact eval value.
        if(exactEval)
        {
          if (iteration - 1 % range == 0)
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                 distance(l, i);
          }
          else
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                   metric.Evaluate(transformedDataset.col(i),
                       transformedDataset.col(impostors(l, i)));
          }
        }

        // Update cache eval value.
        evalOld(l, j, i) = eval;

        // Check bounding condition.
        if (eval <= -1)
        {
          // update bound.
          bp = l;
          break;
        }

        cost += regularization * (1 + eval);
      }
    }
  }

  // Update cache transformation matrix.
  transformationOld = transformation;

  return cost;
};

//! Calculate cost over batches.
template<typename MetricType>
double LMNNFunction<MetricType>::Evaluate(const arma::mat& transformation,
                                          const size_t begin,
                                          const size_t batchSize)
{
  double cost = 0;

  // Ensure cache transformation cube has correct size.
  if (transformationOldPoint.n_elem == 0)
  {
    transformationOldPoint.set_size(transformation.n_rows,
        transformation.n_cols, dataset.n_cols);
    transformationOldPoint.fill(arma::datum::nan);
  }

  // Apply metric over dataset.
  transformedDataset = transformation * dataset;

  if (iteration++ % range == 0)
  {
    // Re-calculate impostors on transformed dataset.
    constraint.Impostors(impostors, distance, transformedDataset, labels,
        begin, batchSize);
  }

  for (size_t i = begin; i < begin + batchSize; i++)
  {
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate cost due to distance between target neighbors & data point.
      double eval = metric.Evaluate(transformedDataset.col(i),
                          transformedDataset.col(targetNeighbors(j, i)));
      cost += (1 - regularization) * eval;
    }

    // Calculate norm of change in transformation.
    double transformationDiff = 0;
    if (!transformationOldPoint.slice(i).has_nan())
    {
      transformationDiff = arma::norm(transformation -
          transformationOldPoint.slice(i));
    }

    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation. Here bp stands for
      // breaking point.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate cost due to {data point, target neighbors, impostors}
        // triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Bounds for eval.
        if (transformationOld.n_elem != 0 && !std::isnan(evalOld(l, j, i)))
        {
          // Update cache max impostor norm.
          maxImpNorm(l, i) = std::max(maxImpNorm(l, i), norm(impostors(l, i)));

          eval = evalOld(l, j, i) + transformationDiff *
            (norm(targetNeighbors(j, i)) + maxImpNorm(l, i) +
            2 * norm(i));

          // Check if there is need to calculate exact eval value.
          if (eval <= -1)
          {
            exactEval = false;
          }
          else
          {
            // Reset cacche max impostor norm.
            maxImpNorm(l, i) = 0;
            evalOld(l, j, i) = arma::datum::nan;
          }
        }

        // Calculate exact eval value.
        if(exactEval)
        {
          if (iteration - 1 % range == 0)
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                 distance(l, i);
          }
          else
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                   metric.Evaluate(transformedDataset.col(i),
                       transformedDataset.col(impostors(l, i)));
          }
        }

        // Update cache eval value.
        evalOld(l, j, i) = eval;

        // Update cache transformation matrix.
        transformationOldPoint.slice(i) = transformation;

        // Check bounding condition.
        if (eval <= -1)
        {
          // update bound.
          bp = l;
          break;
        }

        cost += regularization * (1 + eval);
      }
    }
  }

  return cost;
}

//! Compute gradient over whole dataset.
template<typename MetricType>
template<typename GradType>
void LMNNFunction<MetricType>::Gradient(const arma::mat& transformation,
                                        GradType& gradient)
{
  gradient.zeros(transformation.n_rows, transformation.n_cols);

  // Calculate gradient due to target neighbors.
  arma::mat cij = pCij;

  // Calculate gradient due to impostors.
  arma::mat cil = arma::zeros(dataset.n_rows, dataset.n_rows);

  for (size_t i = 0; i < dataset.n_cols; i++)
  {
    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate gradient due to triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Use eval calualated during Evaluate.
        if (!std::isnan(evalOld(l, j, i)))
        {
          eval = evalOld(l, j, i);
          exactEval = false;
        }

        if (exactEval)
        {
          eval = metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(targetNeighbors(j, i))) -
                    metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(impostors(l, i)));
        }

        // Check bounding condition.
        if (eval < -1)
        {
          // update bound.
          bp = l;
          break;
        }

        // Caculate gradient due to impostors.
        arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
        cil += diff * arma::trans(diff);

        diff = dataset.col(i) - dataset.col(impostors(l, i));
        cil -= diff * arma::trans(diff);
      }
    }
  }

  gradient = 2 * transformation * ((1 - regularization) * cij +
      regularization * cil);
}

//! Compute gradient over a batch of data points.
template<typename MetricType>
template<typename GradType>
void LMNNFunction<MetricType>::Gradient(const arma::mat& transformation,
                                        const size_t begin,
                                        GradType& gradient,
                                        const size_t batchSize)
{
  gradient.zeros(transformation.n_rows, transformation.n_cols);

  arma::mat cij = arma::zeros(dataset.n_rows, dataset.n_rows);
  arma::mat cil = arma::zeros(dataset.n_rows, dataset.n_rows);

  for (size_t i = begin; i < begin + batchSize; i++)
  {
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate gradient due to target neighbors.
      arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
      cij += diff * arma::trans(diff);
    }

    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate gradient due to triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Use eval calualated during Evaluate.
        if (!std::isnan(evalOld(l, j, i)))
        {
          eval = evalOld(l, j, i);
          exactEval = false;
        }

        if (exactEval)
        {
          eval = metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(targetNeighbors(j, i))) -
                    metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(impostors(l, i)));
        }

        // Check bounding condition.
        if (eval < -1)
        {
          // update bound.
          bp = l;
          break;
        }

        // Caculate gradient due to impostors.
        arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
        cil += diff * arma::trans(diff);

        diff = dataset.col(i) - dataset.col(impostors(l, i));
        cil -= diff * arma::trans(diff);
      }
    }
  }

  gradient = 2 * transformation * ((1 - regularization) * cij +
      regularization * cil);
}

//! Compute cost & gradient over whole dataset.
template<typename MetricType>
template<typename GradType>
double LMNNFunction<MetricType>::EvaluateWithGradient(
                                   const arma::mat& transformation,
                                   GradType& gradient)
{
  double cost = 0;

  // Apply metric over dataset.
  transformedDataset = transformation * dataset;

  // Calculate norm of change in transformation.
  double transformationDiff = 0;
  if (transformationOld.n_elem != 0)
  {
    transformationDiff = arma::norm(transformation - transformationOld);
  }

  if (iteration++ % range == 0)
  {
    // Re-calculate impostors on transformed dataset.
    constraint.Impostors(impostors, distance, transformedDataset, labels);
  }

  gradient.zeros(transformation.n_rows, transformation.n_cols);

  // Calculate gradient due to target neighbors.
  arma::mat cij = pCij;

  // Calculate gradient due to impostors.
  arma::mat cil = arma::zeros(dataset.n_rows, dataset.n_rows);

  for (size_t i = 0; i < dataset.n_cols; i++)
  {
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate cost due to distance between target neighbors & data point.
      double eval = metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(targetNeighbors(j, i)));
      cost += (1 - regularization) * eval;
    }

    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate cost due to {data point, target neighbors, impostors}
        // triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Bounds for eval.
        if (transformationOld.n_elem != 0 && !std::isnan(evalOld(l, j, i)))
        {
          // Update cache max impostor norm.
          maxImpNorm(l, i) = std::max(maxImpNorm(l, i), norm(impostors(l, i)));

          eval = evalOld(l, j, i) + transformationDiff *
            (norm(targetNeighbors(j, i)) + maxImpNorm(l, i) +
            2 * norm(i));

          // Check if there is need to calculate exact eval value.
          if (eval <= -1)
          {
            exactEval = false;
          }
          else
          {
            // Reset cacche max impostor norm.
            maxImpNorm(l, i) = 0;
            evalOld(l, j, i) = arma::datum::nan;
          }
        }

        // Calculate exact eval value.
        if(exactEval)
        {
          if (iteration - 1 % range == 0)
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                 distance(l, i);
          }
          else
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                   metric.Evaluate(transformedDataset.col(i),
                       transformedDataset.col(impostors(l, i)));
          }
        }

        // Update cache eval value.
        evalOld(l, j, i) = eval;

        // Check bounding condition.
        if (eval <= -1)
        {
          // update bound.
          bp = l;
          break;
        }

        cost += regularization * (1 + eval);

        // Caculate gradient due to impostors.
        arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
        cil += diff * arma::trans(diff);

        diff = dataset.col(i) - dataset.col(impostors(l, i));
        cil -= diff * arma::trans(diff);
      }
    }
  }

  gradient = 2 * transformation * ((1 - regularization) * cij +
      regularization * cil);

  // Update cache transformation matrix.
  transformationOld = transformation;

  return cost;
}

//! Compute cost & gradient over a batch of data points.
template<typename MetricType>
template<typename GradType>
double LMNNFunction<MetricType>::EvaluateWithGradient(
                                   const arma::mat& transformation,
                                   const size_t begin,
                                   GradType& gradient,
                                   const size_t batchSize)
{
  double cost = 0;

  // Ensure cache transformation cube has correct size.
  if (transformationOldPoint.n_elem == 0)
  {
    transformationOldPoint.set_size(transformation.n_rows,
        transformation.n_cols, dataset.n_cols);
    transformationOldPoint.fill(arma::datum::nan);
  }

  // Apply metric over dataset.
  transformedDataset = transformation * dataset;

  if (iteration++ % range == 0)
  {
    // Re-calculate impostors on transformed dataset.
    constraint.Impostors(impostors, distance, transformedDataset, labels,
        begin, batchSize);
  }

  gradient.zeros(transformation.n_rows, transformation.n_cols);

  arma::mat cij = arma::zeros(dataset.n_rows, dataset.n_rows);
  arma::mat cil = arma::zeros(dataset.n_rows, dataset.n_rows);

  for (size_t i = begin; i < begin + batchSize; i++)
  {
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate cost due to distance between target neighbors & data point.
      double eval = metric.Evaluate(transformedDataset.col(i),
                        transformedDataset.col(targetNeighbors(j, i)));
      cost += (1 - regularization) * eval;

      // Calculate gradient due to target neighbors.
      arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
      cij += diff * arma::trans(diff);
    }

    // Calculate norm of change in transformation.
    double transformationDiff = 0;
    if (!transformationOldPoint.slice(i).has_nan())
    {
      transformationDiff = arma::norm(transformation -
          transformationOldPoint.slice(i));
    }

    for (int j = k - 1; j >= 0; j--)
    {
      // Bound constraints to avoid uneccesary computation.
      for (size_t l = 0, bp = k; l < bp ; l++)
      {
        // Calculate cost due to {data point, target neighbors, impostors}
        // triplets.
        double eval = 0;

        // Flag to trigger exact eval calculation.
        bool exactEval = true;

        // Bounds for eval.
        if (transformationOld.n_elem != 0 && !std::isnan(evalOld(l, j, i)))
        {
          // Update cache max impostor norm.
          maxImpNorm(l, i) = std::max(maxImpNorm(l, i), norm(impostors(l, i)));

          eval = evalOld(l, j, i) + transformationDiff *
            (norm(targetNeighbors(j, i)) + maxImpNorm(l, i) +
            2 * norm(i));

          // Check if there is need to calculate exact eval value.
          if (eval <= -1)
          {
            exactEval = false;
          }
          else
          {
            // Reset cacche max impostor norm.
            maxImpNorm(l, i) = 0;
            evalOld(l, j, i) = arma::datum::nan;
          }
        }

        // Calculate exact eval value.
        if(exactEval)
        {
          if (iteration - 1 % range == 0)
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                 distance(l, i);
          }
          else
          {
            eval = metric.Evaluate(transformedDataset.col(i),
                     transformedDataset.col(targetNeighbors(j, i))) -
                   metric.Evaluate(transformedDataset.col(i),
                       transformedDataset.col(impostors(l, i)));
          }
        }

        // Update cache eval value.
        evalOld(l, j, i) = eval;

        // Update cache transformation matrix.
        transformationOldPoint.slice(i) = transformation;

        // Check bounding condition.
        if (eval <= -1)
        {
          // update bound.
          bp = l;
          break;
        }

        cost += regularization * (1 + eval);

        // Caculate gradient due to impostors.
        arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
        cil += diff * arma::trans(diff);

        diff = dataset.col(i) - dataset.col(impostors(l, i));
        cil -= diff * arma::trans(diff);
      }
    }
  }

  gradient = 2 * transformation * ((1 - regularization) * cij +
      regularization * cil);


  return cost;
}

template<typename MetricType>
inline void LMNNFunction<MetricType>::Precalculate()
{
  pCij.zeros(dataset.n_rows, dataset.n_rows);
  norm.zeros(dataset.n_cols);

  for (size_t i = 0; i < dataset.n_cols; i++)
  {
    // Store norm of each datapoint. Used for bounds.
    norm(i) = arma::norm(dataset.col(i));
    for (size_t j = 0; j < k ; j++)
    {
      // Calculate gradient due to target neighbors.
      arma::vec diff = dataset.col(i) - dataset.col(targetNeighbors(j, i));
      pCij += diff * arma::trans(diff);
    }
  }
}

} // namespace lmnn
} // namespace mlpack

#endif
