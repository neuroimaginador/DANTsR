#' @title labelsToPoints
#' @description return point coordinates of all voxels with specified label
#' @param img a multichannel antsImage of a displacment field
#' @param seeds a matrix of seed points for tracking
#' @param mask a mask of brain vs background'
#' @export
deterministicTracking = function(img, seeds, mask=NULL)
{
  if ( !class(img)=="antsImage") {
    stop("Must input an 'antsImage'")
  }

  .Call( "deterministicTracking", img, seeds, mask, PACKAGE="DANTsR")

}
