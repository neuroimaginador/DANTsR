% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/dtiReconstruction.R
\name{dtiReconstruction}
\alias{dtiReconstruction}
\title{dtiReconstruction}
\usage{
dtiReconstruction(dwi, gradients, method, mask = NULL)
}
\arguments{
\item{dwi}{an N-channel antsImage}

\item{gradients}{Nx4 matrix of gradient directions and b-values}

\item{method}{the reconstruction algorithm to use (default = "itk-svd")}

\item{mask}{'antsImage' mask indicating where to perform reconstruction
\itemize{
  \item{itk-svd}{uses the itk::DiffusionTensor3DReconstructionImageFilter filter}
}}
}
\description{
calculate an anistropy image from DTI
}
