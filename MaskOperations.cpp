/*=========================================================================
 *
 *  Copyright David Doria 2011 daviddoria@gmail.com
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#include "MaskOperations.h"

// STL
#include <stdexcept>

// VTK
#include <vtkImageData.h>

namespace MaskOperations
{

itk::Index<2> FindPixelAcrossHole(const itk::Index<2>& queryPixel, const ITKHelpers::FloatVector2Type& inputDirection, const Mask* const mask)
{
  if(!mask->IsValid(queryPixel))
    {
    throw std::runtime_error("Can only follow valid pixel+vector across a hole.");
    }

  // Determine if 'direction' is pointing inside or outside the hole

  ITKHelpers::FloatVector2Type direction = inputDirection;

  itk::Index<2> nextPixelAlongVector = ITKHelpers::GetNextPixelAlongVector(queryPixel, direction);

  // If the next pixel along the isophote is in bounds and in the hole region of the patch, procede.
  if(mask->GetLargestPossibleRegion().IsInside(nextPixelAlongVector) && mask->IsHole(nextPixelAlongVector))
    {
    // do nothing
    }
  else
    {
    // There is no requirement for the isophote to be pointing a particular orientation, so try to step along the negative isophote.
    direction *= -1.0;
    nextPixelAlongVector = ITKHelpers::GetNextPixelAlongVector(queryPixel, direction);
    }

  // Trace across the hole
  while(mask->IsHole(nextPixelAlongVector))
    {
    nextPixelAlongVector = ITKHelpers::GetNextPixelAlongVector(nextPixelAlongVector, direction);
    if(!mask->GetLargestPossibleRegion().IsInside(nextPixelAlongVector))
      {
      throw std::runtime_error("Helpers::FindPixelAcrossHole could not find a valid neighbor!");
      }
    }

  return nextPixelAlongVector;
}

void ITKImageToVTKImageMasked(const ITKHelpers::FloatVectorImageType* const image, const Mask* const mask,
                              vtkImageData* const outputImage, const unsigned char maskColor[3])
{
  // This function assumes an ND (with N>3) image has the first 3 channels as RGB and extra
  // information in the remaining channels.

  //std::cout << "ITKImagetoVTKRGBImage()" << std::endl;
  if(image->GetNumberOfComponentsPerPixel() < 3)
    {
    std::cerr << "The input image has " << image->GetNumberOfComponentsPerPixel()
              << " components, but at least 3 are required." << std::endl;
    return;
    }

  // Setup and allocate the image data
  //outputImage->SetNumberOfScalarComponents(3);
  //outputImage->SetScalarTypeToUnsignedChar();
  outputImage->SetDimensions(image->GetLargestPossibleRegion().GetSize()[0],
                             image->GetLargestPossibleRegion().GetSize()[1],
                             1);
  //outputImage->AllocateScalars();
  outputImage->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

  // Copy all of the input image pixels to the output image
  itk::ImageRegionConstIteratorWithIndex<ITKHelpers::FloatVectorImageType> imageIterator(image,image->GetLargestPossibleRegion());
  imageIterator.GoToBegin();

  while(!imageIterator.IsAtEnd())
    {
    unsigned char* VTKPixel = static_cast<unsigned char*>(
          outputImage->GetScalarPointer(imageIterator.GetIndex()[0], imageIterator.GetIndex()[1],0));
    if(mask->IsValid(imageIterator.GetIndex()))
      {
      for(unsigned int component = 0; component < 3; component++)
        {
        VTKPixel[component] = static_cast<unsigned char>(imageIterator.Get()[component]);
        }
      }
    else
      {
      for(unsigned int component = 0; component < 3; component++)
        {
        VTKPixel[component] = maskColor[component];
        }
      }

    ++imageIterator;
    }

  outputImage->Modified();
}

itk::ImageRegion<2> RandomRegionInsideHole(const Mask* const mask, const unsigned int halfWidth)
{
  std::vector<itk::Index<2> > holePixels = mask->GetHolePixelsInRegion(mask->GetLargestPossibleRegion());

  itk::ImageRegion<2> randomRegion;

  do
  {
    itk::Index<2> randomPixel = holePixels[rand() % holePixels.size()];
    randomRegion = ITKHelpers::GetRegionInRadiusAroundPixel(randomPixel, halfWidth);
  } while (!mask->IsHole(randomRegion));

  return randomRegion;
}

itk::ImageRegion<2> RandomValidRegion(const Mask* const mask, const unsigned int halfWidth)
{
  // The center pixel must be valid, so we choose one from the valid pixels.
  std::vector<itk::Index<2> > validPixels = mask->GetValidPixelsInRegion(mask->GetLargestPossibleRegion());

  itk::ImageRegion<2> randomRegion;

  do
  {
    itk::Index<2> randomPixel = validPixels[rand() % validPixels.size()];
    randomRegion = ITKHelpers::GetRegionInRadiusAroundPixel(randomPixel, halfWidth);
  } while (!mask->IsValid(randomRegion));

  return randomRegion;
}

itk::ImageRegion<2> ComputeHoleBoundingBox(const Mask* const mask)
{
  itk::ImageRegionConstIteratorWithIndex<Mask> maskIterator(mask, mask->GetLargestPossibleRegion());

  // Initialize backwards
  itk::Index<2> min = {{mask->GetLargestPossibleRegion().GetSize()[0], mask->GetLargestPossibleRegion().GetSize()[1]}};
  itk::Index<2> max = {{0, 0}};

  while(!maskIterator.IsAtEnd())
    {
    itk::Index<2> currentIndex = maskIterator.GetIndex();
    if(mask->IsHole(currentIndex))
    {
      if(currentIndex[0] < min[0])
      {
        min[0] = currentIndex[0];
      }
      if(currentIndex[1] < min[1])
      {
        min[1] = currentIndex[1];
      }
      if(currentIndex[0] > max[0])
      {
        max[0] = currentIndex[0];
      }
      if(currentIndex[1] > max[1])
      {
        max[1] = currentIndex[1];
      }
    }
    ++maskIterator;
    }

  itk::Size<2> size = {{max[0] - min[0] + 1, max[1] - min[1] + 1}}; // The +1's are fencepost error correction
  itk::ImageRegion<2> region(min, size);
  return region;
}


void SetMaskTransparency(const Mask* const input, vtkImageData* outputImage)
{
  // Setup and allocate the VTK image
  outputImage->SetDimensions(input->GetLargestPossibleRegion().GetSize()[0],
                             input->GetLargestPossibleRegion().GetSize()[1],
                             1);

  outputImage->AllocateScalars(VTK_UNSIGNED_CHAR, 4);

  // Copy all of the pixels to the output
  itk::ImageRegionConstIteratorWithIndex<Mask> imageIterator(input, input->GetLargestPossibleRegion());
  imageIterator.GoToBegin();

  while(!imageIterator.IsAtEnd())
    {
    unsigned char* pixel = static_cast<unsigned char*>(outputImage->GetScalarPointer(imageIterator.GetIndex()[0],
                                                                                     imageIterator.GetIndex()[1],0));
    /*
    // Set masked pixels to bright green and opaque. Set non-masked pixels to black and fully transparent.
    pixel[0] = 0;
    pixel[1] = imageIterator.Get();
    pixel[2] = 0;
    */

    // Set masked pixels to bright red and opaque. Set non-masked pixels to black and fully transparent.
    pixel[0] = imageIterator.Get();
    pixel[1] = 0;
    pixel[2] = 0;

    if(input->IsHole(imageIterator.GetIndex()))
      {
      pixel[3] = 255;
      }
    else
      {
      pixel[3] = 0;
      }

    ++imageIterator;
    }

  outputImage->Modified();
}

std::vector<itk::ImageRegion<2> > GetAllFullyValidRegions(const Mask* const mask,
                                                          const itk::ImageRegion<2>& searchRegion,
                                                          const unsigned int patchRadius)
{
  std::vector<itk::ImageRegion<2> > fullyValidRegions;

  itk::ImageRegionConstIteratorWithIndex<Mask> imageIterator(mask, searchRegion);

  itk::Size<2> patchSize = {{patchRadius * 2 + 1, patchRadius * 2 + 1}};

  while(!imageIterator.IsAtEnd())
    {
    itk::ImageRegion<2> region(imageIterator.GetIndex(), patchSize);

    if(searchRegion.IsInside(region) && mask->IsValid(region))
      {
      fullyValidRegions.push_back(region);
      }
    ++imageIterator;
    }

  return fullyValidRegions;
}

std::vector<itk::ImageRegion<2> > GetAllFullyValidRegions(const Mask* const mask, const unsigned int patchRadius)
{
  return GetAllFullyValidRegions(mask, mask->GetLargestPossibleRegion(), patchRadius);
}

itk::ImageRegion<2> GetRandomValidPatchInRegion(const Mask* const mask,
                                                const itk::ImageRegion<2>& searchRegion,
                                                const unsigned int patchRadius,
                                                const unsigned int maxNumberOfAttempts)
{
  unsigned int numberOfAttempts = 0;

  itk::Size<2> patchSize = {{patchRadius * 2 + 1, patchRadius * 2 + 1}};
  itk::ImageRegion<2> region;
  region.SetSize(patchSize);

  do
  {
    int randX = Helpers::RandomInt(searchRegion.GetIndex()[0],
                                   searchRegion.GetIndex()[0] + searchRegion.GetSize()[0] - 1);

    int randY = Helpers::RandomInt(searchRegion.GetIndex()[1],
                                   searchRegion.GetIndex()[1] + searchRegion.GetSize()[1] - 1);

    itk::Index<2> randomIndex = {{randX, randY}};
    //std::cout << "RandomIndex: " << randomIndex << std::endl;
    region.SetIndex(randomIndex);

    numberOfAttempts++;
    if(numberOfAttempts > maxNumberOfAttempts)
    {
      // for now, return the failure situation without trying hard. See if this is the bottleneck.
      itk::Size<2> regionSize = {{0,0}};
      region.SetSize(regionSize);
      return region;
    }
  } while(!(mask->GetLargestPossibleRegion().IsInside(region) && mask->IsValid(region)));

  return region;
}


itk::ImageRegion<2> GetRandomValidPatchInRegion(const Mask* const mask,
                                                const itk::ImageRegion<2>& searchRegion,
                                                const unsigned int patchRadius)
{
  unsigned int numberOfAttempts = 0;

  itk::Size<2> patchSize = {{patchRadius * 2 + 1, patchRadius * 2 + 1}};
  itk::ImageRegion<2> region;
  region.SetSize(patchSize);

  unsigned int maxRandomAttemps = 10;
  do
  {
    int randX = Helpers::RandomInt(searchRegion.GetIndex()[0],
                                   searchRegion.GetIndex()[0] + searchRegion.GetSize()[0] - 1);

    int randY = Helpers::RandomInt(searchRegion.GetIndex()[1],
                                   searchRegion.GetIndex()[1] + searchRegion.GetSize()[1] - 1);

    itk::Index<2> randomIndex = {{randX, randY}};
    //std::cout << "RandomIndex: " << randomIndex << std::endl;
    region.SetIndex(randomIndex);

    numberOfAttempts++;
    if(numberOfAttempts > maxRandomAttemps)
    {
      //std::cout << "Searching all patches in region for a valid patch..." << std::endl;

      // This function is relatively slow.
      std::vector<itk::ImageRegion<2> > allRegions = GetAllFullyValidRegions(mask, searchRegion, patchRadius);
      if(allRegions.size() == 0) // There are actually no valid regions in this searchRegion
      {
        //std::cout << "No valid patch found." << std::endl;
        itk::Size<2> regionSize = {{0,0}};
        region.SetSize(regionSize);
        return region;
      }
      else
      {
//         std::cout << "Valid patch finally found." << std::endl;
//         std::cout << "allRegions.size() = " << allRegions.size() << std::endl;
        unsigned int randomIndex = Helpers::RandomInt(0, allRegions.size() - 1);
//        std::cout << "Returning patch " << randomIndex << std::endl;

        return allRegions[randomIndex];
      }
    }
  } while(!(mask->GetLargestPossibleRegion().IsInside(region) && mask->IsValid(region)));

  return region;
}

} // end namespace
