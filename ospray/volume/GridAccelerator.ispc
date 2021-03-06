// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

// ospray
#include "ospray/volume/GridAccelerator.ih"
#include "ospray/volume/StructuredVolume.ih"
#include "ospray/math/bbox.ih"

//! Bit count used to represent the brick width.
#define BRICK_WIDTH_BITCOUNT (4)

//! Brick width in grid cells.
#define BRICK_WIDTH (1 << BRICK_WIDTH_BITCOUNT)

//! Brick size in cells.
#define BRICK_CELL_COUNT (BRICK_WIDTH * BRICK_WIDTH * BRICK_WIDTH)

//! Bit count used to represent the grid cell width.
#define CELL_WIDTH_BITCOUNT (4)

//! Grid cell width in volumetric elements.
#define CELL_WIDTH (1 << CELL_WIDTH_BITCOUNT)

//! Compute the 1D address of a cell in the grid.
uint32 GridAccelerator_getCellAddress(GridAccelerator *uniform accelerator, const varying vec3i &index);

//! Compute the value range of voxels contained in each cell of a brick in the grid.
task void GridAccelerator_encodeVolumeBrick(GridAccelerator *uniform accelerator, StructuredVolume *uniform volume);

//! Get the volumetric value range of a cell.
inline void GridAccelerator_getCellRange(GridAccelerator *uniform accelerator, const varying vec3i &index, varying vec2f &value)
{
  const uint32 address = GridAccelerator_getCellAddress(accelerator, index);
  value = accelerator->cellRange[address];
}

//! Set the volumetric value range of a cell.
inline void GridAccelerator_setCellRange(GridAccelerator *uniform accelerator, uniform uint32 address, uniform vec2f &value)
{
  accelerator->cellRange[address] = value;
}

GridAccelerator *uniform GridAccelerator_createInstance(void *uniform _volume)
{
  //! Cast to the actual volume type.
  StructuredVolume *uniform volume = (StructuredVolume *uniform)_volume;

  //! The accelerator container.
  GridAccelerator *uniform accelerator = uniform new uniform GridAccelerator;

  //! Grid size in cells per dimension after padding out the volume dimensions to the nearest cell.
  accelerator->gridDimensions = (volume->dimensions + CELL_WIDTH - 1) / CELL_WIDTH;

  //! Grid size in bricks per dimension after padding out the grid dimensions to the nearest brick.
  accelerator->brickCount = (accelerator->gridDimensions + BRICK_WIDTH - 1) / BRICK_WIDTH;

  //! Grid cell count with padding.
  const uniform size_t cellCount = accelerator->brickCount.x * accelerator->brickCount.y * accelerator->brickCount.z * BRICK_CELL_COUNT;

  //! Allocate storage for the volumetric value range per cell.
  accelerator->cellRange = (cellCount > 0) ?  uniform new uniform vec2f[cellCount] : NULL;

  //! Keep a pointer to the volume.
  accelerator->volume = volume;

  //! Compute the volumetric value range per cell.
  launch[accelerator->brickCount.x * accelerator->brickCount.y * accelerator->brickCount.z] GridAccelerator_encodeVolumeBrick(accelerator, volume);

  //! The completed acceleration structure.
  return accelerator;
}

inline void GridAccelerator_encodeBrickCell(GridAccelerator *uniform accelerator, StructuredVolume *uniform volume, const uniform vec3i &cellIndex, uniform vec2f &cellRange)
{
  //! Loop over voxels in the current cell.
  foreach (k = 0 ... CELL_WIDTH, j = 0 ... CELL_WIDTH, i = 0 ... CELL_WIDTH) {

    //! The 3D index of the voxel in the volume.
    const vec3i voxelIndex = cellIndex * CELL_WIDTH + make_vec3i(i, j, k);

    //! The voxel value.
    float value; volume->getVoxel(volume, min(volume->dimensions - 1, voxelIndex), value);

    //! Update the volumetric value range of the current cell, ignoring any NaN values.
    if(!isnan(value))
      cellRange.x = min(cellRange.x, reduce_min(value));  cellRange.y = max(cellRange.y, reduce_max(value));
  }
}

task void GridAccelerator_encodeVolumeBrick(GridAccelerator *uniform accelerator, StructuredVolume *uniform volume)
{
  //! Brick index from task index.
  const uniform vec3i brickIndex = make_vec3i(taskIndex % accelerator->brickCount.x,
                                              (taskIndex / accelerator->brickCount.x) % accelerator->brickCount.y,
                                              taskIndex / (accelerator->brickCount.x*accelerator->brickCount.y));

  //! The 1D address of the brick in the grid.
  uniform uint32 brickAddress = brickIndex.x + accelerator->brickCount.x * (brickIndex.y + accelerator->brickCount.y * (uint32) brickIndex.z);

  //! Loop over cells in the current brick.
  for (uniform uint32 i=0 ; i < BRICK_CELL_COUNT ; i++) {

    //! The 1D address of the cell in the grid.
    uniform uint32 cellAddress = brickAddress << 3 * BRICK_WIDTH_BITCOUNT | i;

    //! The index of the slice containing the current cell.
    uniform uint32 z = i >> 2 * BRICK_WIDTH_BITCOUNT;  uniform uint32 offset = i & (BRICK_WIDTH * BRICK_WIDTH - 1);

    //! The indices of the cell within the slice.
    uniform uint32 y = offset >> BRICK_WIDTH_BITCOUNT;  uniform uint32 x = offset % BRICK_WIDTH;

    //! The 3D index of the cell in the grid.
    uniform vec3i cellIndex = brickIndex * BRICK_WIDTH + make_vec3i(x, y, z);

    //! The minimum and maximum volumetric values contained in the cell.
    uniform vec2f cellRange = make_vec2f(99999.0f, -99999.0f);

    //! Compute the value range over the voxels in the cell.
    GridAccelerator_encodeBrickCell(accelerator, volume, cellIndex, cellRange);

    //! Store the value range.
    GridAccelerator_setCellRange(accelerator, cellAddress, cellRange);
  }
}

inline uint32 GridAccelerator_getCellAddress(GridAccelerator *uniform accelerator, const varying vec3i &index)
{
  //! Compute the 3D index of the grid brick containing the cell.
  const vec3i brickIndex = index >> BRICK_WIDTH_BITCOUNT;

  //! Compute the 1D address of the grid brick containing the cell.
  const uint32 brickAddress = brickIndex.x + accelerator->brickCount.x * (brickIndex.y + accelerator->brickCount.y * (uint32) brickIndex.z);

  //! Compute the 3D offset of the cell in the grid brick.
  const vec3i cellOffset = bitwise_AND(index, BRICK_WIDTH - 1);

  //! Compute the 1D address of the cell.
  return(brickAddress << 3 * BRICK_WIDTH_BITCOUNT | cellOffset.z << 2 * BRICK_WIDTH_BITCOUNT | cellOffset.y << BRICK_WIDTH_BITCOUNT | cellOffset.x);
}

inline box3f GridAccelerator_getCellBounds(GridAccelerator *uniform accelerator, const varying vec3i &index) {

  //! The associated volume.
  StructuredVolume *uniform volume = (StructuredVolume *uniform) accelerator->volume;

  //! Coordinates of the lower corner of the cell in world coordinates.
  vec3f lower; volume->transformLocalToWorld(volume, float_cast(index << CELL_WIDTH_BITCOUNT), lower);

  //! Coordinates of the upper corner of the cell in world coordinates.
  vec3f upper; volume->transformLocalToWorld(volume, float_cast(index + 1 << CELL_WIDTH_BITCOUNT), upper);

  //! The bounding box in world coordinates.
  return(make_box3f(lower, upper));

}

inline vec2f GridAccelerator_intersectCell(const varying box3f &bounds, const varying Ray &ray)
{
  //! Intersection interval minimum per axis.
  const vec3f minimum = rcp(ray.dir) * (bounds.lower - ray.org);

  //! Intersection interval maximum per axis.
  const vec3f maximum = rcp(ray.dir) * (bounds.upper - ray.org);

  //! Distance along the ray to the entry point.
  vec2f interval;  interval.x = max(max(ray.t0, min(minimum.x, maximum.x)), max(min(minimum.y, maximum.y), min(minimum.z, maximum.z)));

  //! Distance along the ray to the exit point.
  interval.y = min(min(ray.t1, max(minimum.x, maximum.x)), min(max(minimum.y, maximum.y), max(minimum.z, maximum.z)));  

  return interval;
}

void GridAccelerator_intersect(GridAccelerator *uniform accelerator, uniform float step, varying Ray &ray)
{
  //! The associated volume.
  StructuredVolume *uniform volume = (StructuredVolume *uniform) accelerator->volume;

  //! Tentatively advance the ray.
  ray.t += step;  if (ray.t >= ray.t1) return;

  //! Compute the hit point in the local coordinate system.
  vec3f localCoordinates; volume->transformWorldToLocal(volume, ray.org + ray.t * ray.dir, localCoordinates);

  //! Compute the 3D index of the cell containing the hit point.
  vec3i cellIndex = integer_cast(localCoordinates) >> CELL_WIDTH_BITCOUNT;

  //! If we visited this cell before then it must not be empty.
  if (ray.geomID == cellIndex.x && ray.primID == cellIndex.y && ray.instID == cellIndex.z) return;

  //! Track the hit cell.
  ray.geomID = cellIndex.x;  ray.primID = cellIndex.y;  ray.instID = cellIndex.z;

  //! Get the volumetric value range of the cell.
  vec2f cellRange;  GridAccelerator_getCellRange(accelerator, cellIndex, cellRange);

  //! Get the maximum opacity in the volumetric value range.
  float maximumOpacity = volume->inherited.transferFunction->getMaxOpacityInRange(volume->inherited.transferFunction, cellRange);

  //! Return the hit point if the grid cell is not fully transparent.
  if (maximumOpacity > 0.0f) return;

  //! Bounds of the grid cell in world coordinates.
  box3f cellBounds = GridAccelerator_getCellBounds(accelerator, cellIndex);

  //! Identify the distance along the ray to the entry and exit points on the cell.
  vec2f cellInterval = GridAccelerator_intersectCell(cellBounds, ray);

  //! Advance the ray so the next hit point will be outside the empty cell.
  ray.t += floor(abs(cellInterval.y - ray.t) / step) * step;

  //! Rinse and repeat.
  GridAccelerator_intersect(accelerator, step, ray);
}

void GridAccelerator_intersectIsosurface(GridAccelerator *uniform accelerator, uniform float step, uniform float *uniform isovalues, uniform int numIsovalues, varying Ray &ray)
{
  //! The associated volume.
  StructuredVolume *uniform volume = (StructuredVolume *uniform) accelerator->volume;

  //! Tentatively advance the ray.
  ray.t += step;
  if (ray.t >= ray.t1) return;

  //! Compute the hit point in the local coordinate system.
  vec3f localCoordinates;
  volume->transformWorldToLocal(volume, ray.org + ray.t * ray.dir, localCoordinates);

  //! Compute the 3D index of the cell containing the hit point.
  vec3i cellIndex = integer_cast(localCoordinates) >> CELL_WIDTH_BITCOUNT;

  //! If we visited this cell before then it must not be empty.
  if (ray.geomID == cellIndex.x && ray.primID == cellIndex.y && ray.instID == cellIndex.z)
    return;

  //! Track the hit cell.
  ray.geomID = cellIndex.x;  ray.primID = cellIndex.y;  ray.instID = cellIndex.z;

  //! Get the volumetric value range of the cell.
  vec2f cellRange;
  GridAccelerator_getCellRange(accelerator, cellIndex, cellRange);

  //! Return the hit point if the grid cell contains an isovalue.
  for (uniform int i=0; i<numIsovalues; i++)
    if (isovalues[i] >= cellRange.x && isovalues[i] <= cellRange.y)
      return;

  //! Bounds of the grid cell in world coordinates.
  box3f cellBounds = GridAccelerator_getCellBounds(accelerator, cellIndex);

  //! Identify the distance along the ray to the entry and exit points on the cell.
  vec2f cellInterval = GridAccelerator_intersectCell(cellBounds, ray);

  //! Advance the ray so the next hit point will be outside the empty cell.
  ray.t += floor(abs(cellInterval.y - ray.t) / step) * step;

  //! Rinse and repeat.
  GridAccelerator_intersectIsosurface(accelerator, step, isovalues, numIsovalues, ray);
}
