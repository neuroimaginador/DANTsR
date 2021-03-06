#include "antsrRegistration.h"

#include "itkVectorImage.h"
#include "itkDefaultConvertPixelTraits.h"
#include "itkAffineTransform.h"
#include "itkTranslationTransform.h"
#include "itkEuler2DTransform.h"
#include "itkEuler3DTransform.h"
#include "itkCenteredAffineTransform.h"
#include "itkAmoebaOptimizerv4.h"
#include "itkExhaustiveOptimizerv4.h"
#include "itkOnePlusOneEvolutionaryOptimizerv4.h"
#include "itkNormalVariateGenerator.h"
#include "itkRegularStepGradientDescentOptimizerv4.h"
#include "itkMeanSquaresImageToImageMetricv4.h"
#include "itkMattesMutualInformationImageToImageMetricv4.h"
#include "itkInterpolateImageFunction.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkNearestNeighborInterpolateImageFunction.h"
#include "itkImageRegistrationMethodv4.h"
#include "itkResampleImageFilter.h"
#include "itkCommandIterationUpdate.h"
#include "itkTimeProbe.h"

#include "itkantsRegistrationHelper.h"


template< class ImageType >
itk::Vector<double, ImageType::ImageDimension> geometricCenterInitialize( typename ImageType::Pointer fixed,
  typename ImageType::Pointer moving)
{
  typedef typename ImageType::IndexType IndexType;
  typedef typename ImageType::PointType PointType;

  IndexType fixedIndex;
  IndexType movingIndex;

  for (unsigned int i=0; i<ImageType::ImageDimension; i++)
    {
    fixedIndex[i] = fixed->GetLargestPossibleRegion().GetSize()[i]/2;
    movingIndex[i] = moving->GetLargestPossibleRegion().GetSize()[i]/2;
    }

  PointType fixedCenter;
  PointType movingCenter;
  fixed->TransformIndexToPhysicalPoint(fixedIndex, fixedCenter);
  moving->TransformIndexToPhysicalPoint(movingIndex, movingCenter);

  itk::Vector<double, ImageType::ImageDimension> offset = movingCenter - fixedCenter;

  return offset;

}

class CommandIterationUpdateHard : public itk::Command
{
public:
  typedef CommandIterationUpdateHard   Self;
  typedef itk::Command             Superclass;
  typedef itk::SmartPointer<Self>  Pointer;
  itkNewMacro( Self );
protected:
  CommandIterationUpdateHard() {};
public:
  typedef itk::RegularStepGradientDescentOptimizerv4<double> OptimizerType;
  typedef const OptimizerType*                               OptimizerPointer;
  void Execute(itk::Object *caller, const itk::EventObject & event) ITK_OVERRIDE
  {
    Execute( (const itk::Object *)caller, event);
  }
  void Execute(const itk::Object * object, const itk::EventObject & event) ITK_OVERRIDE
  {
    OptimizerPointer optimizer = static_cast< OptimizerPointer >( object );
    if( ! itk::IterationEvent().CheckEvent( &event ) )
      {
      return;
      }
    std::cout << optimizer->GetCurrentIteration() << " = ";
    std::cout << optimizer->GetValue() << " : ";
    std::cout << optimizer->GetCurrentPosition() << std::endl;
  }
};


// FIXME - save data to data frame, rather than printing out (or both)
template<class OptimizerType>
class CommandIterationUpdate : public itk::Command
{
public:
  typedef CommandIterationUpdate   Self;
  typedef itk::Command             Superclass;
  typedef itk::SmartPointer<Self>  Pointer;
  //typedef OptimizerType            OptimizerType;
  typedef const OptimizerType*     OptimizerPointer;

  itkNewMacro( Self );
protected:
  CommandIterationUpdate() {};
public:
  //typedef itk::RegularStepGradientDescentOptimizerv4<double> OptimizerType;
  //typedef const OptimizerType*                               OptimizerPointer;
  std::vector<double> m_MetricValues;

  void Execute(itk::Object *caller, const itk::EventObject & event) ITK_OVERRIDE
  {
    Execute( (const itk::Object *)caller, event);
  }
  void Execute(const itk::Object * object, const itk::EventObject & event) ITK_OVERRIDE
  {
    OptimizerPointer optimizer = static_cast< OptimizerPointer >( object );
    if( ! itk::IterationEvent().CheckEvent( &event ) )
      {
      return;
      }
    Rcpp::Rcout << optimizer->GetCurrentIteration() << " = ";
    Rcpp::Rcout << optimizer->GetValue() << " : ";
    Rcpp::Rcout << optimizer->GetCurrentPosition() << std::endl;

    this->m_MetricValues.push_back(optimizer->GetValue());
  }
};



template<class RegistrationType>
SEXP antsrRegistrationMethodRun( SEXP r_registrationObject, SEXP r_fixed, SEXP r_moving )
{

  Rcpp::Rcout << "antsrRegistrationMethodRun<RegistrationType>(SEXP, SEXP, SEXP)" << std::endl;

  itk::TimeProbe timer;
  timer.Start();

  typedef typename RegistrationType::FixedImageType       ImageType;
  typedef typename ImageType::Pointer                     ImagePointerType;
  //typedef typename ImageType::PixelType                   PixelType;
  typedef typename RegistrationType::OutputTransformType  TransformType;
  typedef typename TransformType::ScalarType              PrecisionType;

  // Get parameters from R

  Rcpp::S4 registrationObject( r_registrationObject );
  ImagePointerType fixed = Rcpp::as<ImagePointerType>( r_fixed );
  ImagePointerType moving = Rcpp::as<ImagePointerType>( r_moving );

  Rcpp::NumericVector smoothing( registrationObject.slot("smoothing") );
  Rcpp::NumericVector shrinking( registrationObject.slot("shrink") );
  Rcpp::NumericVector iterations( registrationObject.slot("iterations") );
  Rcpp::NumericVector sampling( registrationObject.slot("sampling") );

  const double convergence = Rcpp::as< double >( registrationObject.slot( "convergence" ) );
  const bool usePhysical =  Rcpp::as< bool >( registrationObject.slot( "smoothingPhysical" ) );

  //const unsigned int nLevels = Rcpp::as< unsigned int>( registrationObject.slot( "nLevels" ) );
  const unsigned int nLevels = smoothing.size();

  std::string metricName = Rcpp::as< std::string >( registrationObject.slot( "metric" ) );
  std::string transformName = Rcpp::as< std::string >( registrationObject.slot( "transform" ) );
  std::string optimizerName = Rcpp::as< std::string >( registrationObject.slot( "optimizer" ) );
  std::string interpolatorName = Rcpp::as< std::string >( registrationObject.slot( "interpolator" ) );
  std::string samplingTypeName = Rcpp::as< std::string >( registrationObject.slot( "samplingStrategy" ) );

  // Now set up the registration
  typename RegistrationType::Pointer   registration  = RegistrationType::New();

  Rcpp::Rcout << "Parameters read...setting up" << std::endl;

  // Only support one optimizer type for now
  typedef itk::ObjectToObjectOptimizerBaseTemplate<PrecisionType>     OptimizerBaseType;
  typedef typename OptimizerBaseType::Pointer                         OptimizerBasePointer;
  OptimizerBasePointer optimizerBase;

  Rcpp::Rcout << "  - Optimizer" << std::endl;
  if ( optimizerName == "regularStepGradientDescent")
    {
    typedef itk::RegularStepGradientDescentOptimizerv4<PrecisionType>   OptimizerType;
    typename OptimizerType::Pointer optimizer  = OptimizerType::New();
    optimizerBase = dynamic_cast<OptimizerBaseType*>(optimizer.GetPointer());
    registration->SetOptimizer( optimizer );

    optimizer->SetLearningRate( 4 );
    optimizer->SetMinimumStepLength( convergence );
    optimizer->SetRelaxationFactor( 0.5 );

    // Setup optimizer-observer
    typedef itk::CommandIterationUpdate<OptimizerType>          ObserverType;
    typename ObserverType::Pointer observer = ObserverType::New();
    registration->GetOptimizer()->AddObserver( itk::IterationEvent(), observer );

    }
  else
    {
    Rcpp::Rcout << "Unsupported optimizer type: " << optimizerName << std::endl;
    return Rcpp::wrap(NA_REAL);
    }



  // FIXME - change this per level
  registration->GetOptimizer()->SetNumberOfIterations( iterations[0] );

  /*
   * Set up interpolators for fixed and moving images
   */
  Rcpp::Rcout << "  - Interpolator" << std::endl;
  typedef itk::InterpolateImageFunction<ImageType,PrecisionType>  InterpolatorBaseType;
  typedef typename InterpolatorBaseType::Pointer                  InterpolatorBasePointer;
  InterpolatorBasePointer fixedInterpolatorBase;
  InterpolatorBasePointer movingInterpolatorBase;

  if ( interpolatorName == "linear")
    {
    typedef itk::LinearInterpolateImageFunction<ImageType, PrecisionType>   InterpolatorType;
    typename InterpolatorType::Pointer   fixedInterpolator  = InterpolatorType::New();
    typename InterpolatorType::Pointer   movingInterpolator  = InterpolatorType::New();
    fixedInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(fixedInterpolator.GetPointer());
    movingInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(movingInterpolator.GetPointer());
    }
  else if ( interpolatorName == "nearestNeighbor")
    {
    typedef itk::NearestNeighborInterpolateImageFunction<ImageType, PrecisionType>   InterpolatorType;
    typename InterpolatorType::Pointer   fixedInterpolator  = InterpolatorType::New();
    typename InterpolatorType::Pointer   movingInterpolator  = InterpolatorType::New();
    fixedInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(fixedInterpolator.GetPointer());
    movingInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(movingInterpolator.GetPointer());
    }
  else
    {
    Rcpp::Rcout << "Unsupported interpolator type: " << interpolatorName << std::endl;
    return Rcpp::wrap(NA_REAL);
    }

  /*
   * Setup image metric
   */
   Rcpp::Rcout << "  - Metric" << std::endl;
  if ( metricName == "mutualInformation")
    {
    typedef itk::MattesMutualInformationImageToImageMetricv4<ImageType,ImageType,ImageType,PrecisionType> MetricType;
    typename MetricType::Pointer metric = MetricType::New();
    metric->SetNumberOfHistogramBins( 32 );
    metric->SetUseMovingImageGradientFilter( false );
    metric->SetUseFixedImageGradientFilter( false );
    metric->SetFixedInterpolator( fixedInterpolatorBase );
    metric->SetMovingInterpolator( movingInterpolatorBase );
    registration->SetMetric( metric );
    }
  else if ( metricName == "meanSquares")
    {
    typedef itk::MeanSquaresImageToImageMetricv4<ImageType,ImageType,ImageType,PrecisionType> MetricType;
    typename MetricType::Pointer metric = MetricType::New();
    metric->SetFixedInterpolator( fixedInterpolatorBase );
    metric->SetMovingInterpolator( movingInterpolatorBase );
    registration->SetMetric( metric );
    }
  else
    {
    Rcpp::Rcout << "Unsupported metric type: " << metricName << std::endl;
    return Rcpp::wrap(NA_REAL);
    }

  /*
   * Setup registration
   */
  registration->SetFixedImage( fixed );
  registration->SetMovingImage( moving );

  typename RegistrationType::ShrinkFactorsArrayType shrinkFactorsPerLevel;
  shrinkFactorsPerLevel.SetSize( nLevels );

  typename RegistrationType::SmoothingSigmasArrayType smoothingSigmasPerLevel;
  smoothingSigmasPerLevel.SetSize( nLevels );

  typename RegistrationType::MetricSamplingPercentageArrayType metricSampling;
  metricSampling.SetSize( nLevels );

  for ( unsigned int i=0; i<nLevels; i++)
    {
    shrinkFactorsPerLevel[i] = shrinking[i];
    smoothingSigmasPerLevel[i] = smoothing[i];
    metricSampling[i] = sampling[i];
    // = maxIterations[i];
    }

  Rcpp::Rcout << "  - Registration" << std::endl;
  registration->SetNumberOfLevels ( nLevels );
  registration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
  registration->SetSmoothingSigmasAreSpecifiedInPhysicalUnits( usePhysical );
  registration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
  registration->SetMetricSamplingPercentagePerLevel( metricSampling );

  if ( samplingTypeName == "random")
    {
    registration->SetMetricSamplingStrategy( RegistrationType::RANDOM );
    }
  else if (samplingTypeName == "regular")
    {
    registration->SetMetricSamplingStrategy( RegistrationType::REGULAR );
    }


  /*
   * Initialize with a translation
   */
  Rcpp::Rcout << "  - Transform" << std::endl;
  typename itk::Vector<double,ImageType::ImageDimension> offset
    = geometricCenterInitialize<ImageType>(fixed,moving);

  typename TransformType::Pointer movingInitialTransform = TransformType::New();
  movingInitialTransform->SetIdentity();
  registration->SetMovingInitialTransform( movingInitialTransform );

  typename TransformType::Pointer  identityTransform = TransformType::New();
  identityTransform->SetIdentity();
  registration->SetFixedInitialTransform( identityTransform );

  if ( ImageType::ImageDimension == 2 )
    {

    typename TransformType::ParametersType initialParameters(
      registration->GetMovingInitialTransform()->GetNumberOfParameters() );

    if ( transformName == "translation")
      {
      initialParameters[0] = offset[0];
      initialParameters[1] = offset[1];
      movingInitialTransform->SetParameters( initialParameters );

      typedef typename OptimizerBaseType::ScalesType       OptimizerScalesType;
      OptimizerScalesType optimizerScales( movingInitialTransform->GetNumberOfParameters() );
      optimizerScales[0] = 1.0;
      optimizerScales[1] = 1.0;
      registration->GetOptimizer()->SetScales( optimizerScales );
      }
    else if (transformName == "rigid")
      {
      initialParameters[0] = 0;
      initialParameters[1] = 0;
      initialParameters[2] = 0;
      initialParameters[3] = offset[0];
      initialParameters[4] = offset[1];
      initialParameters[5] = offset[2];
      movingInitialTransform->SetParameters( initialParameters );
      //registration->SetMovingInitialTransform( movingInitialTransform );

      typedef typename OptimizerBaseType::ScalesType       OptimizerScalesType;
      OptimizerScalesType optimizerScales( movingInitialTransform->GetNumberOfParameters() );
      const double translationScale = 1.0 / 1000.0;
      optimizerScales[0] = 1.0;
      optimizerScales[1] = 1.0;
      optimizerScales[2] = 1.0;
      optimizerScales[3] = translationScale;
      optimizerScales[4] = translationScale;
      optimizerScales[5] = translationScale;
      registration->GetOptimizer()->SetScales( optimizerScales );
      }
    }
  else if ( ImageType::ImageDimension == 3 )
    {

    typename TransformType::ParametersType initialParameters(
    registration->GetMovingInitialTransform()->GetNumberOfParameters() );

    if ( transformName == "translation")
      {
      initialParameters[0] = offset[0];
      initialParameters[1] = offset[1];
      initialParameters[2] = offset[2];
      movingInitialTransform->SetParameters( initialParameters );

      typedef typename OptimizerBaseType::ScalesType       OptimizerScalesType;
      OptimizerScalesType optimizerScales( movingInitialTransform->GetNumberOfParameters() );
      optimizerScales[0] = 1.0;
      optimizerScales[1] = 1.0;
      optimizerScales[2] = 1.0;
      registration->GetOptimizer()->SetScales( optimizerScales );
      }
    else if (transformName == "rigid")
      {
      initialParameters[0] = 0;
      initialParameters[1] = 0;
      initialParameters[2] = 0;
      initialParameters[3] = offset[0];
      initialParameters[4] = offset[1];
      initialParameters[5] = offset[2];
      movingInitialTransform->SetParameters( initialParameters );
      //registration->SetMovingInitialTransform( movingInitialTransform );

      typedef typename OptimizerBaseType::ScalesType       OptimizerScalesType;
      OptimizerScalesType optimizerScales( movingInitialTransform->GetNumberOfParameters() );
      const double translationScale = 1.0 / 1000.0;
      optimizerScales[0] = 1.0;
      optimizerScales[1] = 1.0;
      optimizerScales[2] = 1.0;
      optimizerScales[3] = translationScale;
      optimizerScales[4] = translationScale;
      optimizerScales[5] = translationScale;
      registration->GetOptimizer()->SetScales( optimizerScales );
      }
    }


  Rcpp::Rcout << "  - registration->Update()" << std::endl;
  try
  {
    registration->DebugOn();
    registration->Update();
    //Rcpp::Rcout << "Optimizer stop condition: "
    //<< registration->GetOptimizer()->GetStopConditionDescription()
    //<< std::endl;
  }
  catch( itk::ExceptionObject & err )
  {
    Rcpp::Rcout << "ExceptionObject caught !" << std::endl;
    Rcpp::Rcout << err << std::endl;
    return(Rcpp::wrap(NA_REAL));
  }

  Rcpp::Rcout << "  - get outputs" << std::endl;

  typename TransformType::ConstPointer transform = registration->GetTransform();
  typename TransformType::ParametersType finalParameters = transform->GetParameters();

  //const unsigned int numberOfIterations = optimizer->GetCurrentIteration();
  //const double bestValue = optimizer->GetValue();

  typedef itk::CompositeTransform<PrecisionType,ImageType::ImageDimension> CompositeTransformType;
  typename CompositeTransformType::Pointer outputCompositeTransform =
    CompositeTransformType::New();
  outputCompositeTransform->AddTransform( movingInitialTransform );
  outputCompositeTransform->AddTransform( registration->GetModifiableTransform() );

  // Print out results
  //Rcpp::Rcout << "Result = " << std::endl;
  //Rcpp::Rcout << "All Parameters = ";
  //for (unsigned int i=0; i<outputCompositeTransform->GetNumberOfParameters(); i++ )
  //  {
    //Rcpp::Rcout << " " << outputCompositeTransform->GetParameters()[i];
  //  }
  //Rcpp::Rcout << std::endl;
  //Rcpp::Rcout << " Iterations    = " << numberOfIterations << std::endl;
  //Rcpp::Rcout << " Metric value  = " << bestValue          << std::endl;


  typedef itk::ResampleImageFilter<ImageType,ImageType,PrecisionType,PrecisionType>  ResampleFilterType;
  typename ResampleFilterType::Pointer resampler = ResampleFilterType::New();
  resampler->SetInput( moving );
  resampler->SetTransform( outputCompositeTransform );
  resampler->SetSize( fixed->GetLargestPossibleRegion().GetSize() );
  resampler->SetOutputOrigin(  fixed->GetOrigin() );
  resampler->SetOutputSpacing( fixed->GetSpacing() );
  resampler->SetOutputDirection( fixed->GetDirection() );
  resampler->SetDefaultPixelValue( 0 );
  resampler->Update();

  timer.Stop();

  //Rcpp::NumericVector metricValues;

  // Get iteration-wise values from optimizer observer
  if ( optimizerName == "regularStepGradientDescent")
    {

    }


  //return Rcpp::List::create(Rcpp::Named("metricValues") = Rcpp::wrap(observer->m_MetricValues),
  //                          Rcpp::Named("warpedImage") = alignedImage,
  //                          Rcpp::Named("time") = Rcpp::wrap( timer.GetMean() ));

  return Rcpp::List::create(Rcpp::Named("warpedImage") = Rcpp::wrap( static_cast<ImagePointerType>(resampler->GetOutput()) ),
                            Rcpp::Named("time") = Rcpp::wrap( timer.GetMean() ));

}

template< class ImageType >
SEXP antsrRegistrationMethod( SEXP r_registrationObject, SEXP r_fixed, SEXP r_moving )
{
  Rcpp::Rcout << "antsrRegistrationMethod<ImageType>(SEXP, SEXP, SEXP)" << std::endl;

  //typedef typename ImageType::Pointer                                  ImagePointerType;
  //typedef typename ImageType::PixelType                                PixelType;

  Rcpp::S4 registrationObject( r_registrationObject );
  std::string transformName = Rcpp::as< std::string >( registrationObject.slot( "transform" ) );
  std::string precision = Rcpp::as< std::string >( registrationObject.slot( "precision" ) );

  if ( precision == "double")
    {
    typedef double PrecisionType;
    if ( transformName == "translation")
      {
      typedef itk::TranslationTransform<PrecisionType,ImageType::ImageDimension>  TransformType;
      typedef itk::ImageRegistrationMethodv4<ImageType,ImageType,TransformType>   RegistrationType;

      return antsrRegistrationStage<RegistrationType>( r_registrationObject, r_fixed, r_moving );
      }
    else if ( transformName == "rigid")
      {
      typedef typename RigidTransformTraits<PrecisionType,ImageType::ImageDimension>::TransformType TransformType;
      typedef itk::ImageRegistrationMethodv4<ImageType,ImageType,TransformType>                     RegistrationType;
      return antsrRegistrationStage<RegistrationType>( r_registrationObject, r_fixed, r_moving );
      }
    else
      {
      Rcpp::Rcout << "Unsupported transform type: " << transformName << std::endl;
      return Rcpp::wrap(NA_REAL);
      }
    }
  if ( precision == "float")
    {
    typedef float PrecisionType;

    if ( transformName == "translation")
      {
      typedef itk::TranslationTransform<PrecisionType,ImageType::ImageDimension>  TransformType;
      typedef itk::ImageRegistrationMethodv4<ImageType,ImageType,TransformType>   RegistrationType;

      return antsrRegistrationWithHelper<RegistrationType>( r_registrationObject, r_fixed, r_moving );
      }
    else if ( transformName == "rigid")
      {
      typedef typename RigidTransformTraits<PrecisionType,ImageType::ImageDimension>::TransformType  TransformType;
      typedef itk::ImageRegistrationMethodv4<ImageType,ImageType,TransformType>              RegistrationType;
      return antsrRegistrationStage<RegistrationType>( r_registrationObject, r_fixed, r_moving );
      }
    else
      {
      Rcpp::Rcout << "Unsupported transform type: " << transformName << std::endl;
      return Rcpp::wrap(NA_REAL);
      }
    }
  else
    {
    Rcpp::Rcout << "Unsupported compute precision: " << precision << std::endl;
    return Rcpp::wrap(NA_REAL);
    }

}

template<class HelperType>
void setupRegistrationHelper( typename HelperType::Pointer helper, SEXP r_registrationObject )
{
  Rcpp::Rcout << "setupRegistrationHelper(X,Y)" << std::endl;
  nullStream cnul;
  helper->SetLogStream(cnul);
  helper->SetLogStream( Rcpp::Rcout );



}

template<class RegistrationType>
SEXP antsrRegistrationWithHelper( SEXP r_registrationObject, SEXP r_fixed, SEXP r_moving )
{
  Rcpp::Rcout << "antsrRegistrationWithHelper(SEXP,SEXP,SEXP)" << std::endl;
  typedef typename RegistrationType::OutputTransformType  TransformType;
  typedef typename TransformType::ScalarType              PrecisionType;
  typedef typename RegistrationType::FixedImageType       ImageType;
  typedef typename ImageType::Pointer                     ImagePointerType;
  typedef typename ants::RegistrationHelper<PrecisionType, ImageType::ImageDimension> RegistrationHelperType;

  ImagePointerType fixed = Rcpp::as<ImagePointerType>( r_fixed );
  ImagePointerType moving = Rcpp::as<ImagePointerType>( r_moving );

  typename RegistrationHelperType::Pointer regHelper = RegistrationHelperType::New();


  setupRegistrationHelper<RegistrationHelperType>( regHelper, r_registrationObject );


  return Rcpp::wrap(NA_REAL);
}


template< class RegistrationType >
SEXP antsrRegistrationStage( SEXP r_registrationObject, SEXP r_fixed, SEXP r_moving )
{
  Rcpp::Rcout << "antsrRegistrationTest<ImageType>(SEXP,SEXP,SEXP)" << std::endl;

  typedef typename RegistrationType::OutputTransformType  TransformType;
  typedef typename TransformType::ScalarType              PrecisionType;
  typedef typename RegistrationType::FixedImageType       ImageType;
  typedef typename ImageType::Pointer                     ImagePointerType;

  ImagePointerType fixed = Rcpp::as<ImagePointerType>( r_fixed );
  ImagePointerType moving = Rcpp::as<ImagePointerType>( r_moving );

  Rcpp::S4 registrationObject( r_registrationObject );
  std::string transformName = Rcpp::as< std::string >( registrationObject.slot( "transform" ) );
  std::string interpolatorName = Rcpp::as< std::string >( registrationObject.slot( "interpolator" ) );
  std::string metricName = Rcpp::as< std::string >( registrationObject.slot( "metric" ) );
  Rcpp::NumericVector sampling( registrationObject.slot("sampling") );
  std::string samplingStrategy = Rcpp::as< std::string >( registrationObject.slot( "samplingStrategy" ) );
  Rcpp::NumericVector smoothing( registrationObject.slot("smoothing") );
  Rcpp::NumericVector shrinking( registrationObject.slot("shrink") );
  unsigned int nLevels = Rcpp::as<unsigned int > ( registrationObject.slot("nLevels") );

  //typedef itk::TranslationTransform< double, ImageType::ImageDimension > TransformType;

  typedef itk::RegularStepGradientDescentOptimizerv4<PrecisionType> OptimizerType;

  typedef itk::MeanSquaresImageToImageMetricv4<ImageType,ImageType,ImageType,PrecisionType>    MetricType;

  //typedef itk::ImageRegistrationMethodv4<ImageType,ImageType,TransformType>    RegistrationType;

  //typename MetricType::Pointer         metric        = MetricType::New();
  typename OptimizerType::Pointer      optimizer     = OptimizerType::New();
  typename RegistrationType::Pointer   registration  = RegistrationType::New();

  //typedef itk::ObjectToObjectOptimizerBaseTemplate<PrecisionType>     OptimizerBaseType;
  //typedef typename OptimizerBaseType::Pointer                         OptimizerBasePointer;
  //OptimizerBasePointer optimizerBase;

  //typedef itk::LinearInterpolateImageFunction<ImageType,double> FixedLinearInterpolatorType;
  //typedef itk::LinearInterpolateImageFunction<ImageType,double> MovingLinearInterpolatorType;

  /** Interpolator **/
  typedef itk::InterpolateImageFunction<ImageType,PrecisionType>  InterpolatorBaseType;
  typedef typename InterpolatorBaseType::Pointer                  InterpolatorBasePointer;
  InterpolatorBasePointer fixedInterpolatorBase;
  InterpolatorBasePointer movingInterpolatorBase;

  if ( interpolatorName == "linear")
    {
    typedef itk::LinearInterpolateImageFunction<ImageType, PrecisionType>   InterpolatorType;
    typename InterpolatorType::Pointer   fixedInterpolator  = InterpolatorType::New();
    typename InterpolatorType::Pointer   movingInterpolator  = InterpolatorType::New();
    fixedInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(fixedInterpolator.GetPointer());
    movingInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(movingInterpolator.GetPointer());
    }
  else if ( interpolatorName == "nearestNeighbor")
    {
    typedef itk::NearestNeighborInterpolateImageFunction<ImageType, PrecisionType>   InterpolatorType;
    typename InterpolatorType::Pointer   fixedInterpolator  = InterpolatorType::New();
    typename InterpolatorType::Pointer   movingInterpolator  = InterpolatorType::New();
    fixedInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(fixedInterpolator.GetPointer());
    movingInterpolatorBase = dynamic_cast<InterpolatorBaseType*>(movingInterpolator.GetPointer());
    }
  else
    {
    Rcpp::Rcout << "Unsupported interpolator type: " << interpolatorName << std::endl;
    return Rcpp::wrap(NA_REAL);
    }


  /** Image Metric **/
  if ( metricName == "mutualInformation")
    {
    typedef itk::MattesMutualInformationImageToImageMetricv4<ImageType,ImageType,ImageType,PrecisionType> MetricType;
    typename MetricType::Pointer metric = MetricType::New();
    metric->SetNumberOfHistogramBins( 32 );
    metric->SetUseMovingImageGradientFilter( false );
    metric->SetUseFixedImageGradientFilter( false );
    metric->SetFixedInterpolator( fixedInterpolatorBase );
    metric->SetMovingInterpolator( movingInterpolatorBase );
    registration->SetMetric( metric );
    }
  else if ( metricName == "meanSquares")
    {
    typedef itk::MeanSquaresImageToImageMetricv4<ImageType,ImageType,ImageType,PrecisionType> MetricType;
    typename MetricType::Pointer metric = MetricType::New();
    metric->SetFixedInterpolator( fixedInterpolatorBase );
    metric->SetMovingInterpolator( movingInterpolatorBase );
    registration->SetMetric( metric );
    }
  else
    {
    Rcpp::Rcout << "Unsupported metric type: " << metricName << std::endl;
    return Rcpp::wrap(NA_REAL);
    }

  typename RegistrationType::MetricSamplingPercentageArrayType metricSampling;
  metricSampling.SetSize( nLevels );

  typename RegistrationType::ShrinkFactorsArrayType shrinkFactorsPerLevel;
  shrinkFactorsPerLevel.SetSize( nLevels );

  typename RegistrationType::SmoothingSigmasArrayType smoothingSigmasPerLevel;
  smoothingSigmasPerLevel.SetSize( nLevels );

  for ( unsigned int i=0; i<nLevels; i++)
    {
    shrinkFactorsPerLevel[i] = shrinking[i];
    smoothingSigmasPerLevel[i] = smoothing[i];
    metricSampling[i] = sampling[i];
    // = maxIterations[i];
    }

  registration->SetNumberOfLevels ( nLevels );
  registration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
  //registration->SetSmoothingSigmasAreSpecifiedInPhysicalUnits( usePhysical );
  registration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
  registration->SetMetricSamplingPercentagePerLevel( metricSampling );

  if ( samplingStrategy == "random")
    {
    registration->SetMetricSamplingStrategy( RegistrationType::RANDOM );
    }
  else if (samplingStrategy == "regular")
    {
    registration->SetMetricSamplingStrategy( RegistrationType::REGULAR );
    }



  //registration->SetMetric(        metric        );
  registration->SetOptimizer(     optimizer     );



  registration->SetFixedImage( fixed );
  registration->SetMovingImage( moving );

  itk::Vector<double, ImageType::ImageDimension> offset = geometricCenterInitialize<ImageType>(fixed, moving);
  Rcpp::Rcout << offset << std::endl;

  typename TransformType::Pointer movingInitialTransform = TransformType::New();
  typename TransformType::ParametersType initialParameters(
    movingInitialTransform->GetNumberOfParameters() );

  movingInitialTransform->SetIdentity();

  if ( transformName == "translation" ) {
    for ( unsigned int i=0; i<movingInitialTransform->GetNumberOfParameters(); i++ )
    {
    initialParameters[i] = 0; //offset[i];  // Initial offset in mm along X
    }
  }
  else if ( transformName == "rigid" ) {
    for ( unsigned int i=0; i<movingInitialTransform->GetNumberOfParameters(); i++ )
    {
    initialParameters[i] = 0;
    }
  }

  movingInitialTransform->SetParameters( initialParameters );
  registration->SetMovingInitialTransform( movingInitialTransform );

  typename TransformType::Pointer   identityTransform = TransformType::New();
  identityTransform->SetIdentity();
  registration->SetFixedInitialTransform( identityTransform );

  optimizer->SetLearningRate( 4 );
  optimizer->SetMinimumStepLength( 0.001 );
  optimizer->SetRelaxationFactor( 0.5 );
  //optimizer->SetGradientMagnitudeTolerance( 0.00001 );

/*
  bool useEstimator = false;
  if( useEstimator )
    {
    typedef itk::RegistrationParameterScalesFromPhysicalShift<MetricType> ScalesEstimatorType;
    typename ScalesEstimatorType::Pointer scalesEstimator = ScalesEstimatorType::New();
    scalesEstimator->SetMetric( registration->GetMetric() );
    scalesEstimator->SetTransformForward( true );
    optimizer->SetScalesEstimator( scalesEstimator );
    optimizer->SetDoEstimateLearningRateOnce( true );
    }
*/

  optimizer->SetNumberOfIterations( 200 );

  typename CommandIterationUpdate<OptimizerType>::Pointer observer = CommandIterationUpdate<OptimizerType>::New();
  optimizer->AddObserver( itk::IterationEvent(), observer );

  /*
  const unsigned int numberOfLevels = 1;
  typename RegistrationType::ShrinkFactorsArrayType shrinkFactorsPerLevel;
  shrinkFactorsPerLevel.SetSize( 1 );
  shrinkFactorsPerLevel[0] = 1;
  typename RegistrationType::SmoothingSigmasArrayType smoothingSigmasPerLevel;
  smoothingSigmasPerLevel.SetSize( 1 );
  smoothingSigmasPerLevel[0] = 0;
  registration->SetNumberOfLevels ( numberOfLevels );
  registration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
  registration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
  */

  try
    {
    registration->Update();
    std::cout << "Optimizer stop condition: "
    << registration->GetOptimizer()->GetStopConditionDescription()
    << std::endl;
    }
  catch( itk::ExceptionObject & err )
    {
    std::cerr << "ExceptionObject caught !" << std::endl;
    std::cerr << err << std::endl;
    return Rcpp::wrap(NA_REAL);
    }


  return Rcpp::wrap(NA_REAL);
}

RcppExport SEXP antsrRegistrationRun( SEXP r_registrationObject, SEXP r_fixed, SEXP r_moving )
try
{

  Rcpp::Rcout << "antsrRegistrationRun(SEXP, SEXP, SEXP)" << std::endl;

  if ( r_registrationObject == NULL )
    {
    Rcpp::Rcout << "NULL itkImageRegistrationMethod object passed" << std::endl;
    return Rcpp::wrap(NA_REAL);
    }

  Rcpp::S4 registration( r_registrationObject );
  Rcpp::S4 itk_fixedimage( r_fixed );
  Rcpp::S4 itk_movingimage( r_moving );

  std::string pixeltype = Rcpp::as< std::string >( itk_fixedimage.slot( "pixeltype" ) );
  unsigned int dimension = Rcpp::as< int >( itk_fixedimage.slot( "dimension" ) );
  unsigned int components = Rcpp::as< int >(itk_fixedimage.slot( "components") );

  if ( components == 1 )
    {
    if( pixeltype == "double" )
      {
      Rcpp::Rcout << "PixelType=double" << std::endl;
      if (dimension == 2)
        {
        Rcpp::Rcout << "Dimension=2" << std::endl;
        //typedef itk::Image<double,2> ImageType;
        //return antsrRegistrationTest<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else if (dimension == 3)
        {
        Rcpp::Rcout << "Dimension=3" << std::endl;
        //typedef itk::Image<double,3> ImageType;
        //return antsrRegistrationTest<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else if (dimension == 4)
        {
        Rcpp::Rcout << "Dimension=4" << std::endl;
        //typedef itk::Image<double,4> ImageType;
        //return antsrRegistrationTest<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else
        {
        Rcpp::Rcout << "Unsupported image dimension" << std::endl;
        return(Rcpp::wrap(NA_REAL));
        }
      }
    else if( pixeltype == "float" )
      {
      Rcpp::Rcout << "PixelType=float" << std::endl;
      if (dimension == 2)
        {
        Rcpp::Rcout << "Dimension=2" << std::endl;
        typedef itk::Image<float,2> ImageType;
        return antsrRegistrationMethod<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else if (dimension == 3)
        {
        Rcpp::Rcout << "Dimension=3" << std::endl;
        typedef itk::Image<float,3> ImageType;
        return antsrRegistrationMethod<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else if (dimension == 4)
        {
        Rcpp::Rcout << "Dimension=4" << std::endl;
        typedef itk::Image<float,4> ImageType;
        return antsrRegistrationMethod<ImageType>(r_registrationObject, r_fixed, r_moving);
        }
      else
        {
        Rcpp::Rcout << "Unsupported image dimension" << std::endl;
        return(Rcpp::wrap(NA_REAL));
        }
      }
    else if ( pixeltype == "integer")
      {
      Rcpp::Rcout << "Integer pixeltype is not yet supported" << std::endl;
      return(Rcpp::wrap(NA_REAL));
      }
    }
  else
    {
    Rcpp::Rcout << "Multi channel images not yet supported" << std::endl;
    return(Rcpp::wrap(NA_REAL));
    }

  return Rcpp::wrap( 1 );
}
catch( const std::exception& exc )
{
  Rcpp::Rcout<< exc.what() << std::endl ;
  return Rcpp::wrap( 1 ) ;
}
