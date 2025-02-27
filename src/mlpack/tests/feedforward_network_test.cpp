/**
 * @file tests/feedforward_network_test.cpp
 * @author Marcus Edel
 * @author Palash Ahuja
 *
 * Tests the feed forward network.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */
#include <mlpack/core.hpp>

#include <mlpack/methods/ann/layer/layer.hpp>
#include <mlpack/methods/ann/loss_functions/mean_squared_error.hpp>
#include <mlpack/methods/ann/ffn.hpp>

#include <ensmallen.hpp>

#include "catch.hpp"
#include "serialization.hpp"
#include "custom_layer.hpp"

using namespace mlpack;
using namespace mlpack::ann;

/**
 * Train and evaluate a model with the specified structure.
 */
template<typename MatType = arma::mat, typename ModelType>
void TestNetwork(ModelType& model,
                 MatType& trainData,
                 MatType& trainLabels,
                 MatType& testData,
                 MatType& testLabels,
                 const size_t maxEpochs,
                 const double classificationErrorThreshold)
{
  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, maxEpochs * trainData.n_cols, -1);
  model.Train(trainData, trainLabels, opt);

  MatType predictionTemp;
  model.Predict(testData, predictionTemp);
  MatType prediction = arma::zeros<MatType>(1, predictionTemp.n_cols);

  for (size_t i = 0; i < predictionTemp.n_cols; ++i)
  {
    prediction(i) = arma::as_scalar(arma::find(
        arma::max(predictionTemp.col(i)) == predictionTemp.col(i), 1));
  }

  size_t correct = arma::accu(prediction == testLabels);
  double classificationError = 1 - double(correct) / testData.n_cols;
  REQUIRE(classificationError <= classificationErrorThreshold);
}

// network1 should be allocated with `new`, and trained on some data.
template<typename MatType = arma::mat, typename ModelType>
void CheckCopyFunction(ModelType* network1,
                       MatType& trainData,
                       MatType& trainLabels,
                       const size_t maxEpochs)
{
  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, maxEpochs * trainData.n_cols, -1);
  network1->Train(trainData, trainLabels, opt);

  arma::mat predictions1;
  network1->Predict(trainData, predictions1);
  FFN<> network2;
  network2 = *network1;
  delete network1;

  // Deallocating all of network1's memory, so that network2 does not use any
  // of that memory.
  arma::mat predictions2;
  network2.Predict(trainData, predictions2);
  CheckMatrices(predictions1, predictions2);
}

// network1 should be allocated with `new`, and trained on some data.
template<typename MatType = arma::mat, typename ModelType>
void CheckMoveFunction(ModelType* network1,
                       MatType& trainData,
                       MatType& trainLabels,
                       const size_t maxEpochs)
{
  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, maxEpochs * trainData.n_cols, -1);
  network1->Train(trainData, trainLabels, opt);

  arma::mat predictions1;
  network1->Predict(trainData, predictions1);
  FFN<> network2(std::move(*network1));
  delete network1;

  // Deallocating all of network1's memory, so that network2 does not use any
  // of that memory.
  arma::mat predictions2;
  network2.Predict(trainData, predictions2);
  CheckMatrices(predictions1, predictions2);
}

/**
 * Check whether copying and moving Vanila network is working or not.
 */
TEST_CASE("CheckCopyMovingVanillaNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  // Normalize labels to [0, 2].
  arma::mat trainLabels = trainData.row(trainData.n_rows - 1) - 1;
  trainData.shed_row(trainData.n_rows - 1);

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
   * network structure looks like:
   *
   *  Input         Hidden        Output
   *  Layer         Layer         Layer
   * +-----+       +-----+       +-----+
   * |     |       |     |       |     |
   * |     +------>|     +------>|     |
   * |     |     +>|     |     +>|     |
   * +-----+     | +--+--+     | +-----+
   *             |             |
   *  Bias       |  Bias       |
   *  Layer      |  Layer      |
   * +-----+     | +-----+     |
   * |     |     | |     |     |
   * |     +-----+ |     +-----+
   * |     |       |     |
   * +-----+       +-----+
   */

  FFN<NegativeLogLikelihood<> > *model = new FFN<NegativeLogLikelihood<> >;
  model->Add<Linear<> >(trainData.n_rows, 8);
  model->Add<SigmoidLayer<> >();
  model->Add<Linear<> >(8, 3);
  model->Add<LogSoftMax<> >();

  FFN<NegativeLogLikelihood<> > *model1 = new FFN<NegativeLogLikelihood<> >;
  model1->Add<Linear<> >(trainData.n_rows, 8);
  model1->Add<SigmoidLayer<> >();
  model1->Add<Linear<> >(8, 3);
  model1->Add<LogSoftMax<> >();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model, trainData, trainLabels, 1);

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model1, trainData, trainLabels, 1);
}

/**
 * Check whether copying and moving network with Reparametrization is working or not.
 */
TEST_CASE("CheckCopyMovingReparametrizationNetworkTest",
          "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  data::Load("thyroid_train.csv", trainData, true);

  // Normalize labels to [0, 2].
  arma::mat trainLabels = trainData.row(trainData.n_rows - 1) - 1;
  trainData.shed_row(trainData.n_rows - 1);

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * followed by a linear layer and then a reparametrization layer.
   */

  FFN<NegativeLogLikelihood<> > *model = new FFN<NegativeLogLikelihood<> >;
  model->Add<Linear<> >(trainData.n_rows, 8);
  model->Add<Reparametrization<> >(4, false, true, 1);
  model->Add<LogSoftMax<> >();

  FFN<NegativeLogLikelihood<> > *model1 = new FFN<NegativeLogLikelihood<> >;
  model1->Add<Linear<> >(trainData.n_rows, 8);
  model1->Add<Reparametrization<> >(4, false, true, 1);
  model1->Add<LogSoftMax<> >();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model, trainData, trainLabels, 1);

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model1, trainData, trainLabels, 1);
}

/**
 * Check whether copying and moving network with linear3d is working or not.
 */
TEST_CASE("CheckCopyMovingLinear3DNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  data::Load("thyroid_train.csv", trainData, true);

  // Normalize labels to [0, 2].
  arma::mat trainLabels = trainData.row(trainData.n_rows - 1) - 1;
  trainData.shed_row(trainData.n_rows - 1);

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
   * network structure looks like:
   *
   *  Input         Hidden        Output
   *  Layer         Layer         Layer
   * +-----+       +-----+       +-----+
   * |     |       |     |       |     |
   * |     +------>|     +------>|     |
   * |     |     +>|     |     +>|     |
   * +-----+     | +--+--+     | +-----+
   *             |             |
   *  Bias       |  Bias       |
   *  Layer      |  Layer      |
   * +-----+     | +-----+     |
   * |     |     | |     |     |
   * |     +-----+ |     +-----+
   * |     |       |     |
   * +-----+       +-----+
   */

  FFN<NegativeLogLikelihood<> > *model = new FFN<NegativeLogLikelihood<> >;
  model->Add<Linear<> >(trainData.n_rows, 8);
  model->Add<SigmoidLayer<> >();
  model->Add<Linear3D<> >(8, 3);
  model->Add<LogSoftMax<> >();

  FFN<NegativeLogLikelihood<> > *model1 = new FFN<NegativeLogLikelihood<> >;
  model1->Add<Linear<> >(trainData.n_rows, 8);
  model1->Add<SigmoidLayer<> >();
  model1->Add<Linear3D<> >(8, 3);
  model1->Add<LogSoftMax<> >();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model, trainData, trainLabels, 1);

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model1, trainData, trainLabels, 1);
}

/**
 * Check whether copying and moving of Noisy Linear layer is working or not.
 */
TEST_CASE("CheckCopyMovingNoisyLinearTest", "[FeedForwardNetworkTest]")
{
  // Create training input by 10x1 matrix (only 1 point).
  arma::mat input = arma::randu(10, 1);
  // Create training output by 1-point matrix.
  arma::mat output = arma::mat("0");

  // Check copying constructor.
  FFN<NegativeLogLikelihood<>> *model1 = new FFN<NegativeLogLikelihood<>>();
  model1->Predictors() = input;
  model1->Responses() = output;
  model1->Add<IdentityLayer<>>();
  model1->Add<NoisyLinear<>>(10, 5);
  model1->Add<Linear<> >(5, 1);
  model1->Add<LogSoftMax<>>();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model1, input, output, 1);

  // Check moving constructor.
  FFN<NegativeLogLikelihood<>> *model2 = new FFN<NegativeLogLikelihood<>>();
  model2->Predictors() = input;
  model2->Responses() = output;
  model2->Add<IdentityLayer<>>();
  model2->Add<NoisyLinear<>>(10, 5);
  model2->Add<Linear<> >(5, 1);
  model2->Add<LogSoftMax<>>();

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model2, input, output, 1);
}

/**
 * Check whether copying and moving of concatenate layer is working or not.
 */
TEST_CASE("CheckCopyMovingConcatenateTest", "[FeedForwardNetworkTest]")
{
  // Create training input by 5x5 matrix.
  arma::mat input = arma::randu(10, 1);
  // Create training output by 1 matrix.
  arma::mat output = arma::mat("1");

  // Check copying constructor.
  FFN<NegativeLogLikelihood<>> *model1 = new FFN<NegativeLogLikelihood<>>();
  model1->Predictors() = input;
  model1->Responses() = output;
  model1->Add<IdentityLayer<>>();
  model1->Add<Linear<>>(10, 5);

  // Create concatenate layer.
  arma::mat concatMatrix = arma::ones(5, 1);
  Concatenate<>* concatLayer = new Concatenate<>();
  concatLayer->Concat() = concatMatrix;

  // Add concatenate layer to the current network.
  model1->Add(concatLayer);
  model1->Add<Linear<> >(10, 5);
  model1->Add<LogSoftMax<>>();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model1, input, output, 1);

  // Check moving constructor.
  FFN<NegativeLogLikelihood<>> *model2 = new FFN<NegativeLogLikelihood<>>();
  model2->Predictors() = input;
  model2->Responses() = output;
  model2->Add<IdentityLayer<>>();
  model2->Add<Linear<>>(10, 5);

  // Create new concat layer.
  Concatenate<>* concatLayer2 = new Concatenate<>();
  concatLayer2->Concat() = concatMatrix;

  // Add concatenate layer to the current network.
  model2->Add(concatLayer2);
  model2->Add<Linear<> >(10, 5);
  model2->Add<LogSoftMax<>>();

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model2, input, output, 1);
}

/**
 * Check whether copying and moving of Dropout network is working or not.
 */
TEST_CASE("CheckCopyMovingDropoutNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  data::Load("thyroid_train.csv", trainData, true);

  // Normalize labels to [0, 2].
  arma::mat trainLabels = trainData.row(trainData.n_rows - 1) - 1;
  trainData.shed_row(trainData.n_rows - 1);

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
   * network structure looks like:
   *
   *  Input         Hidden        Output
   *  Layer         Layer         Layer
   * +-----+       +-----+       +-----+
   * |     |       |     |       |     |
   * |     +------>|     +------>|     |
   * |     |     +>|     |     +>|     |
   * +-----+     | +--+--+     | +-----+
   *             |             |
   *  Bias       |  Bias       |
   *  Layer      |  Layer      |
   * +-----+     | +-----+     |
   * |     |     | |     |     |
   * |     +-----+ |     +-----+
   * |     |       |     |
   * +-----+       +-----+
   */

  FFN<NegativeLogLikelihood<> > *model = new FFN<NegativeLogLikelihood<> >;
  model->Add<Linear<> >(trainData.n_rows, 8);
  model->Add<SigmoidLayer<> >();
  model->Add<Dropout<> >(0.3);
  model->Add<Linear<> >(8, 3);
  model->Add<LogSoftMax<> >();

  FFN<NegativeLogLikelihood<> > *model1 = new FFN<NegativeLogLikelihood<> >;
  model1->Add<Linear<> >(trainData.n_rows, 8);
  model1->Add<SigmoidLayer<> >();
  model1->Add<Dropout<> >(0.3);
  model1->Add<Linear<> >(8, 3);
  model1->Add<LogSoftMax<> >();

  // Check whether copy constructor is working or not.
  CheckCopyFunction<>(model, trainData, trainLabels, 1);

  // Check whether move constructor is working or not.
  CheckMoveFunction<>(model1, trainData, trainLabels, 1);
}

/**
 * Train the vanilla network on a larger dataset.
 */
TEST_CASE("FFVanillaNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // Labels should be from 0 to numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // Labels should be from 0 to numClasses - 1.

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
   * network structure looks like:
   *
   *  Input         Hidden        Output
   *  Layer         Layer         Layer
   * +-----+       +-----+       +-----+
   * |     |       |     |       |     |
   * |     +------>|     +------>|     |
   * |     |     +>|     |     +>|     |
   * +-----+     | +--+--+     | +-----+
   *             |             |
   *  Bias       |  Bias       |
   *  Layer      |  Layer      |
   * +-----+     | +-----+     |
   * |     |     | |     |     |
   * |     +-----+ |     +-----+
   * |     |       |     |
   * +-----+       +-----+
   */

  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<SigmoidLayer<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significant better than 92%.
  TestNetwork<>(model, trainData, trainLabels, testData, testLabels, 10, 0.1);

  arma::mat dataset;
  dataset.load("mnist_first250_training_4s_and_9s.arm");

  // Normalize each point since these are images.
  for (size_t i = 0; i < dataset.n_cols; ++i)
    dataset.col(i) /= norm(dataset.col(i), 2);

  arma::mat labels = arma::zeros(1, dataset.n_cols);
  labels.submat(0, labels.n_cols / 2, 0, labels.n_cols - 1).fill(1);

  FFN<NegativeLogLikelihood<> > model1;
  model1.Add<Linear<> >(dataset.n_rows, 10);
  model1.Add<SigmoidLayer<> >();
  model1.Add<Linear<> >(10, 2);
  model1.Add<LogSoftMax<> >();
  // Vanilla neural net with logistic activation function.
  TestNetwork<>(model1, dataset, labels, dataset, labels, 10, 0.2);
}

TEST_CASE("ForwardBackwardTest", "[FeedForwardNetworkTest]")
{
  arma::mat dataset;
  dataset.load("mnist_first250_training_4s_and_9s.arm");

  // Normalize each point since these are images.
  for (size_t i = 0; i < dataset.n_cols; ++i)
    dataset.col(i) /= norm(dataset.col(i), 2);

  arma::mat labels = arma::zeros(1, dataset.n_cols);
  labels.submat(0, labels.n_cols / 2, 0, labels.n_cols - 1).fill(1);

  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(dataset.n_rows, 50);
  model.Add<SigmoidLayer<> >();
  model.Add<Linear<> >(50, 10);
  model.Add<LogSoftMax<> >();

  ens::VanillaUpdate opt;
  model.ResetParameters();
  #if ENS_VERSION_MAJOR == 1
  opt.Initialize(model.Parameters().n_rows, model.Parameters().n_cols);
  #else
  ens::VanillaUpdate::Policy<arma::mat, arma::mat> optPolicy(opt,
      model.Parameters().n_rows, model.Parameters().n_cols);
  #endif
  double stepSize = 0.01;
  size_t batchSize = 10;

  size_t iteration = 0;
  bool converged = false;
  while (iteration < 100)
  {
    arma::running_stat<double> error;
    size_t batchStart = 0;
    while (batchStart < dataset.n_cols)
    {
      size_t batchEnd = std::min(batchStart + batchSize,
          (size_t) dataset.n_cols);
      arma::mat currentData = dataset.cols(batchStart, batchEnd - 1);
      arma::mat currentLabels = labels.cols(batchStart, batchEnd - 1);
      arma::mat currentResuls;
      model.Forward(currentData, currentResuls);
      arma::mat gradients;
      model.Backward(currentData, currentLabels, gradients);
      #if ENS_VERSION_MAJOR == 1
      opt.Update(model.Parameters(), stepSize, gradients);
      #else
      optPolicy.Update(model.Parameters(), stepSize, gradients);
      #endif
      batchStart = batchEnd;

      arma::mat prediction = arma::zeros<arma::mat>(1, currentResuls.n_cols);

      for (size_t i = 0; i < currentResuls.n_cols; ++i)
      {
        prediction(i) = arma::as_scalar(arma::find(
            arma::max(currentResuls.col(i)) == currentResuls.col(i), 1));
      }

      size_t correct = arma::accu(prediction == currentLabels);
      error(1 - (double) correct / batchSize);
    }
    Log::Debug << "Current training error: " << error.mean() << std::endl;
    iteration++;
    if (error.mean() < 0.05)
    {
      converged = true;
      break;
    }
  }

  REQUIRE(converged);
}

/**
 * Train the dropout network on a larger dataset.
 */
TEST_CASE("DropoutNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // Labels should be from 0 to numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // Labels should be from 0 to numClasses - 1.

  /*
   * Construct a feed forward network with trainData.n_rows input nodes,
   * hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
   * network structure looks like:
   *
   *  Input         Hidden        Dropout      Output
   *  Layer         Layer         Layer        Layer
   * +-----+       +-----+       +-----+       +-----+
   * |     |       |     |       |     |       |     |
   * |     +------>|     +------>|     +------>|     |
   * |     |     +>|     |       |     |       |     |
   * +-----+     | +--+--+       +-----+       +-----+
   *             |
   *  Bias       |
   *  Layer      |
   * +-----+     |
   * |     |     |
   * |     +-----+
   * |     |
   * +-----+
   */

  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<SigmoidLayer<> >();
  model.Add<Dropout<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significant better than 92%.
  TestNetwork<>(model, trainData, trainLabels, testData, testLabels, 10, 0.1);
  arma::mat dataset;
  dataset.load("mnist_first250_training_4s_and_9s.arm");

  // Normalize each point since these are images.
  for (size_t i = 0; i < dataset.n_cols; ++i)
  {
    dataset.col(i) /= norm(dataset.col(i), 2);
  }

  arma::mat labels = arma::zeros(1, dataset.n_cols);
  labels.submat(0, labels.n_cols / 2, 0, labels.n_cols - 1).fill(1);

  FFN<NegativeLogLikelihood<> > model1;
  model1.Add<Linear<> >(dataset.n_rows, 10);
  model1.Add<SigmoidLayer<> >();
  model.Add<Dropout<> >();
  model1.Add<Linear<> >(10, 2);
  model1.Add<LogSoftMax<> >();
  // Vanilla neural net with logistic activation function.
  TestNetwork<>(model1, dataset, labels, dataset, labels, 10, 0.2);
}

/**
 * Train the highway network on a larger dataset.
 */
TEST_CASE("HighwayNetworkTest", "[FeedForwardNetworkTest]")
{
  arma::mat dataset;
  dataset.load("mnist_first250_training_4s_and_9s.arm");

  // Normalize each point since these are images.
  for (size_t i = 0; i < dataset.n_cols; ++i)
    dataset.col(i) /= norm(dataset.col(i), 2);

  arma::mat labels = arma::zeros(1, dataset.n_cols);
  labels.submat(0, labels.n_cols / 2, 0, labels.n_cols - 1).fill(1);

  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(dataset.n_rows, 10);
  Highway<>* highway = new Highway<>(10, true);
  highway->Add<Linear<> >(10, 10);
  highway->Add<SigmoidLayer<> >();
  model.Add(highway); // This takes ownership of the memory.
  model.Add<Linear<> >(10, 2);
  model.Add<LogSoftMax<> >();
  TestNetwork<>(model, dataset, labels, dataset, labels, 10, 0.2);
}

/**
 * Train the DropConnect network on a larger dataset.
 */
TEST_CASE("DropConnectNetworkTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The range should be between 0 and numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The range should be between 0 and numClasses - 1.

 /*
  *  Construct a feed forward network with trainData.n_rows input nodes,
  *  hiddenLayerSize hidden nodes and trainLabels.n_rows output nodes. The
  *  network struct that looks like:
  *
  *  Input         Hidden     DropConnect     Output
  *  Layer         Layer         Layer        Layer
  * +-----+       +-----+       +-----+       +-----+
  * |     |       |     |       |     |       |     |
  * |     +------>|     +------>|     +------>|     |
  * |     |     +>|     |       |     |       |     |
  * +-----+     | +--+--+       +-----+       +-----+
  *             |
  *  Bias       |
  *  Layer      |
  * +-----+     |
  * |     |     |
  * |     +-----+
  * |     |
  * +-----+
  *
  *
  */

  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<SigmoidLayer<> >();
  model.Add<DropConnect<> >(8, 3);
  model.Add<LogSoftMax<> >();

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significant better than 92%.
  TestNetwork<>(model, trainData, trainLabels, testData, testLabels, 10, 0.1);

  arma::mat dataset;
  dataset.load("mnist_first250_training_4s_and_9s.arm");

  // Normalize each point since these are images.
  for (size_t i = 0; i < dataset.n_cols; ++i)
    dataset.col(i) /= norm(dataset.col(i), 2);

  arma::mat labels = arma::zeros(1, dataset.n_cols);
  labels.submat(0, labels.n_cols / 2, 0, labels.n_cols - 1).fill(1);

  FFN<NegativeLogLikelihood<> > model1;
  model1.Add<Linear<> >(dataset.n_rows, 10);
  model1.Add<SigmoidLayer<> >();
  model1.Add<DropConnect<> >(10, 2);
  model1.Add<LogSoftMax<> >();
  // Vanilla neural net with logistic activation function.
  TestNetwork<>(model1, dataset, labels, dataset, labels, 10, 0.2);
}

/**
 * Test miscellaneous things of FFN,
 * e.g. copy/move constructor, assignment operator.
 */
TEST_CASE("FFNMiscTest", "[FeedForwardNetworkTest]")
{
  FFN<MeanSquaredError<>> model;
  model.Add<Linear<>>(2, 3);
  model.Add<ReLULayer<>>();

  auto copiedModel(model);
  copiedModel = model;
  auto movedModel(std::move(model));
  auto moveOperator = std::move(copiedModel);
}

/**
 * Test that serialization works ok.
 */
TEST_CASE("FFSerializationTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The labels should be between 0 and numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The labels should be between 0 and numClasses - 1.

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significant better than 92%.
  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<SigmoidLayer<> >();
  model.Add<Dropout<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, trainData.n_cols /* 1 epoch */, -1);

  model.Train(trainData, trainLabels, opt);

  FFN<NegativeLogLikelihood<>> xmlModel, jsonModel, binaryModel;
  xmlModel.Add<Linear<>>(10, 10); // Layer that will get removed.

  // Serialize into other models.
  SerializeObjectAll(model, xmlModel, jsonModel, binaryModel);

  arma::mat predictions, xmlPredictions, jsonPredictions, binaryPredictions;
  model.Predict(testData, predictions);
  xmlModel.Predict(testData, xmlPredictions);
  jsonModel.Predict(testData, jsonPredictions);
  jsonModel.Predict(testData, binaryPredictions);

  CheckMatrices(predictions, xmlPredictions, jsonPredictions,
      binaryPredictions);
}

/**
 * Test that serialization works ok for PReLU.
 */
TEST_CASE("PReLUSerializationTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The labels should be between 0 and numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The labels should be between 0 and numClasses - 1.

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significant better than 92%.
  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<PReLU<> >();
  model.Add<Dropout<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, trainData.n_cols /* 1 epoch */, -1);

  model.Train(trainData, trainLabels, opt);

  FFN<NegativeLogLikelihood<>> xmlModel, jsonModel, binaryModel;
  xmlModel.Add<Linear<>>(10, 10); // Layer that will get removed.

  // Serialize into other models.
  SerializeObjectAll(model, xmlModel, jsonModel, binaryModel);

  arma::mat predictions, xmlPredictions, jsonPredictions, binaryPredictions;
  model.Predict(testData, predictions);
  xmlModel.Predict(testData, xmlPredictions);
  jsonModel.Predict(testData, jsonPredictions);
  jsonModel.Predict(testData, binaryPredictions);

  CheckMatrices(predictions, xmlPredictions, jsonPredictions,
      binaryPredictions);
}

/**
 * Test if the custom layers work. The target is to see if the code compiles
 * when the Train and Prediction are called.
 */
TEST_CASE("CustomLayerTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The labels should be between 0 and numClasses - 1.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The labels should be between 0 and numClasses - 1.

  FFN<NegativeLogLikelihood<>, RandomInitialization, CustomLayer<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<CustomLayer<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, 15, -1);
  model.Train(trainData, trainLabels, opt);

  arma::mat predictionTemp;
  model.Predict(testData, predictionTemp);
  arma::mat prediction = arma::zeros<arma::mat>(1, predictionTemp.n_cols);
}

/**
 * Test the overload of Forward function which allows partial forward pass.
 */
TEST_CASE("PartialForwardTest", "[FeedForwardNetworkTest]")
{
  FFN<NegativeLogLikelihood<>, RandomInitialization> model;
  model.Add<Linear<> >(5, 10);

  // Add a new Add<> module which adds a constant term to the input.
  Add<>* addModule = new Add<>(10);
  model.Add(addModule);

  LinearNoBias<>* linearNoBiasModule = new LinearNoBias<>(10, 10);
  model.Add(linearNoBiasModule);

  model.Add<Linear<> >(10, 10);

  model.ResetParameters();
  // Set the parameters of the Add<> module to a matrix of ones.
  addModule->Parameters() = arma::ones(10, 1);
  // Set the parameters of the LinearNoBias<> module to a matrix of ones.
  linearNoBiasModule->Parameters() = arma::ones(10, 10);

  arma::mat input = arma::ones(10, 1);
  arma::mat output;

  // Forward pass only through the Add module.
  model.Forward(input,
                output,
                1 /* Index of the Add module */,
                1 /* Index of the Add module */);

  // As we only forward pass through Add module, input and output should
  // differ by a matrix of ones.
  CheckMatrices(input, output - 1);

  // Forward pass only through the Add module and the LinearNoBias module.
  model.Forward(input,
                output,
                1 /* Index of the Add module */,
                2 /* Index of the LinearNoBias module */);

  // As we only forward pass through Add module followed by the LinearNoBias
  // module, output should be a matrix of 20s.(output = weight * input)
  CheckMatrices(output, arma::ones(10, 1) * 20);
}

/**
 * Test that FFN::Train() returns finite objective value.
 */
TEST_CASE("FFNTrainReturnObjective", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The labels should be between 0 and numClasses.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The labels should be between 0 and numClasses.

  // Vanilla neural net with logistic activation function.
  // Because 92% of the patients are not hyperthyroid the neural
  // network must be significantly better than 92%.
  FFN<NegativeLogLikelihood<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<SigmoidLayer<> >();
  model.Add<Dropout<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  ens::RMSProp opt(0.01, 32, 0.88, 1e-8, trainData.n_cols /* 1 epoch */, -1);

  double objVal = model.Train(trainData, trainLabels, opt);

  REQUIRE(std::isfinite(objVal) == true);
}

/**
 * Test that FFN::Model() allows us to access the instantiated network.
 */
TEST_CASE("FFNReturnModel", "[FeedForwardNetworkTest]")
{
  // Create dummy network.
  FFN<NegativeLogLikelihood<> > model;
  Linear<>* linearA = new Linear<>(3, 3);
  model.Add(linearA);
  Linear<>* linearB = new Linear<>(3, 4);
  model.Add(linearB);

  // Initialize network parameter.
  model.ResetParameters();

  // Set all network parameter to one.
  model.Parameters().ones();

  // Zero the second layer parameter.
  linearB->Parameters().zeros();

  // Get the layer parameter from layer A and layer B and store them in
  // parameterA and parameterB.
  arma::mat parameterA, parameterB;
  boost::apply_visitor(ParametersVisitor(parameterA), model.Model()[0]);
  boost::apply_visitor(ParametersVisitor(parameterB), model.Model()[1]);

  CheckMatrices(parameterA, arma::ones(3 * 3 + 3, 1));
  CheckMatrices(parameterB, arma::zeros(3 * 4 + 4, 1));

  CheckMatrices(linearA->Parameters(), arma::ones(3 * 3 + 3, 1));
  CheckMatrices(linearB->Parameters(), arma::zeros(3 * 4 + 4, 1));
}

/**
 * Test to see if the FFN code compiles when the Optimizer
 * doesn't have the MaxIterations() method.
 */
TEST_CASE("OptimizerTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  arma::mat trainLabels = trainData.row(trainData.n_rows - 1);
  trainData.shed_row(trainData.n_rows - 1);
  trainLabels -= 1; // The labels should be between 0 and numClasses.

  arma::mat testData;
  if (!data::Load("thyroid_test.csv", testData))
    FAIL("Cannot load dataset thyroid_test.csv");

  arma::mat testLabels = testData.row(testData.n_rows - 1);
  testData.shed_row(testData.n_rows - 1);
  testLabels -= 1; // The labels should be between 0 and numClasses.

  FFN<NegativeLogLikelihood<>, RandomInitialization, CustomLayer<> > model;
  model.Add<Linear<> >(trainData.n_rows, 8);
  model.Add<CustomLayer<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  ens::DE opt(200, 1000, 0.6, 0.8, 1e-5);
  model.Train(trainData, trainLabels, opt);
}

/**
 * Test to see if an exception is thrown when input with
 * wrong shape is provided to a FFN.
 */
TEST_CASE("FFNCheckInputShapeTest", "[FeedForwardNetworkTest]")
{
  // Load the dataset.
  arma::mat trainData;
  if (!data::Load("thyroid_train.csv", trainData))
    FAIL("Cannot open thyroid_train.csv");

  // Normalize labels to [0, 2].
  arma::mat trainLabels = trainData.row(trainData.n_rows - 1) - 1;
  trainData.shed_row(trainData.n_rows - 1);

  arma::mat testData;
  data::Load("thyroid_test.csv", testData, true);

  arma::mat testLabels = testData.row(testData.n_rows - 1) - 1;
  testData.shed_row(testData.n_rows - 1);

  FFN<NegativeLogLikelihood<>, RandomInitialization, CustomLayer<> > model;
  // Purposely putting wrong input shape so that error is thrown.
  model.Add<Linear<> >(trainData.n_rows - 3, 8);
  model.Add<CustomLayer<> >();
  model.Add<Linear<> >(8, 3);
  model.Add<LogSoftMax<> >();

  std::string expectedMsg = "FFN<>::Train(): ";
  expectedMsg += "the first layer of the network expects ";
  expectedMsg += std::to_string(trainData.n_rows - 3) + " elements, ";
  expectedMsg += "but the input has " + std::to_string(trainData.n_rows) +
      " dimensions! ";

  ens::DE opt(200, 1000, 0.6, 0.8, 1e-5);

  REQUIRE_THROWS_AS(model.Train(trainData, trainLabels, opt), std::logic_error);
}
