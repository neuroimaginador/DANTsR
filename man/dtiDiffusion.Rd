% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/dtiDiffusion.R
\name{dtiDiffusion}
\alias{dtiDiffusion}
\title{dtiDiffusion}
\usage{
dtiDiffusion(img, method = "MeanDiffusion")
}
\arguments{
\item{img}{a 6-channel antsImage or and eigen sytem list}

\item{method}{the summary meaure to return
\itemize{
  \item{Trace}{Sum of eigenvalues}
  \item{MeanDiffusion}{Average eigenvalue}
  \item{AxialDiffusion}{Largest eigenvalue}
  \item{RadialDiffusion}{Mean of two smallest eigenvalues}
}}
}
\description{
calculate an anistropy image from DTI
}
