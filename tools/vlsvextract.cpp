  
/*
This file is part of Vlasiator.

Copyright 2010, 2011, 2012, 2013 Finnish Meteorological Institute
 */


#include <cstdlib>
#include <iostream>

#include <limits>
#include <stdint.h>
#include <cmath>
#include <list>
#include <silo.h>
#include <sstream>
#include <dirent.h>
#include <stdio.h>

#include "vlsvreader2.h"
#include "definitions.h"
#include "vlsv_reader.h"
#include "vlsv_writer.h"
#include "vlsvreaderinterface.h"
#include <vlsv_amr.h>

#include <array> //std::array is from here
#include <unordered_set> //std::unordered_set from here
#include <boost/program_options.hpp>
#include <Eigen/Dense>

#include "phiprof.hpp"


using namespace std;

using namespace Eigen;
using namespace vlsv;
namespace po = boost::program_options;



static DBfile* fileptr = NULL; // Pointer to file opened by SILO

template<typename REAL> struct NodeCrd {
   static REAL EPS;
   REAL x;
   REAL y;
   REAL z;

   NodeCrd(const REAL& x, const REAL& y, const REAL& z) : x(x), y(y), z(z) {
   }

   bool comp(const NodeCrd<REAL>& n) const {
      REAL EPS1, EPS2, EPS;
      EPS1 = 1.0e-6 * fabs(x);
      EPS2 = 1.0e-6 * fabs(n.x);
      if (x == 0.0) EPS1 = 1.0e-7;
      if (n.x == 0.0) EPS2 = 1.0e-7;
      EPS = max(EPS1, EPS2);
      if (fabs(x - n.x) > EPS) return false;

      EPS1 = 1.0e-6 * fabs(y);
      EPS2 = 1.0e-6 * fabs(n.y);
      if (y == 0.0) EPS1 = 1.0e-7;
      if (n.y == 0.0) EPS2 = 1.0e-7;
      EPS = max(EPS1, EPS2);
      if (fabs(y - n.y) > EPS) return false;

      EPS1 = 1.0e-6 * fabs(z);
      EPS2 = 1.0e-6 * fabs(n.z);
      if (z == 0.0) EPS1 = 1.0e-7;
      if (n.z == 0.0) EPS2 = 1.0e-7;
      EPS = max(EPS1, EPS2);
      if (fabs(z - n.z) > EPS) return false;
      return true;
   }
};

struct NodeComp {

   bool operator()(const NodeCrd<double>& a, const NodeCrd<double>& b) const {
      double EPS = 0.5e-3 * (fabs(a.z) + fabs(b.z));
      if (a.z > b.z + EPS) return false;
      if (a.z < b.z - EPS) return true;

      EPS = 0.5e-3 * (fabs(a.y) + fabs(b.y));
      if (a.y > b.y + EPS) return false;
      if (a.y < b.y - EPS) return true;

      EPS = 0.5e-3 * (fabs(a.x) + fabs(b.x));
      if (a.x > b.x + EPS) return false;
      if (a.x < b.x - EPS) return true;
      return false;
   }

   bool operator()(const NodeCrd<float>& a, const NodeCrd<float>& b) const {
      float EPS = 0.5e-3 * (fabs(a.z) + fabs(b.z));
      if (a.z > b.z + EPS) return false;
      if (a.z < b.z - EPS) return true;

      EPS = 0.5e-3 * (fabs(a.y) + fabs(b.y));
      if (a.y > b.y + EPS) return false;
      if (a.y < b.y - EPS) return true;

      EPS = 0.5e-3 * (fabs(a.x) + fabs(b.x));
      if (a.x > b.x + EPS) return false;
      if (a.x < b.x - EPS) return true;
      return false;
   }
};


//A struct for holding info on cell structure (the grid)
struct CellStructure {
   uint64_t cell_bounds[3];     /**< The number of cells in x, y, z direction (initialized somewhere in read parameters).*/
   Real cell_length[3];         /**< Length of a cell in x, y, z direction. */
   Real min_coordinates[3];     /**< x_min, y_min, z_min are stored here.*/
   uint64_t vcell_bounds[3];    /**< The number of cells in x, y, z direction (initialized somewhere in read parameters).*/
   Real vblock_length[3];       /**< Size (dvx,dvy,dvz) of a velocity block in vx, vy, vz directions.*/
   Real min_vcoordinates[3];    /**< vx_min, vy_min, vz_min are stored here.*/
   uint32_t maxVelRefLevel;     /**< Maximum refinement level of velocity meshes.*/

   int slicedCoords[3];
   Real slicedCoordValues[3];
};

//A class for holding user options
class UserOptions {
public:
   bool getCellIdFromLine;
   bool getCellIdFromInput;
   bool getCellIdFromCoordinates;
   bool rotateVectors;
   bool plasmaFrame;
   uint64_t cellId;
   uint32_t numberOfCoordinatesInALine;
   vector<string> outputDirectoryPath;
   array<Real, 3> coordinates;
   array<Real, 3> point1;
   array<Real, 3> point2;
   UserOptions() {
      getCellIdFromLine = false;
      getCellIdFromInput = false;
      getCellIdFromCoordinates = false;
      rotateVectors = false;
      cellId = numeric_limits<uint64_t>::max();
      numberOfCoordinatesInALine = 0;
   }
   ~UserOptions() {}
};



int SiloType(const VLSV::datatype& dataType, const uint64_t& dataSize) {
   switch (dataType) {
      case VLSV::INT:
         if (dataSize == 2) return DB_SHORT;
         else if (dataSize == 4) return DB_INT;
         else if (dataSize == 8) return DB_LONG;
         else return -1;
         break;
      case VLSV::UINT:
         if (dataSize == 2) return DB_SHORT;
         else if (dataSize == 4) return DB_INT;
         else if (dataSize == 8) return DB_LONG;
         else return -1;
         break;
      case VLSV::FLOAT:
         if (dataSize == 4) return DB_FLOAT;
         else if (dataSize == 8) return DB_DOUBLE;
         else return -1;
         break;
      case VLSV::UNKNOWN:
         cout << "BAD DATATYPE AT " << __FILE__ << " " << __LINE__ << endl;
         break;
   }
   return -1;
}

int SiloType(const datatype::type & dataType, const uint64_t & dataSize) {
   switch (dataType) {
      case datatype::type::INT:
         if (dataSize == 2) return DB_SHORT;
         else if (dataSize == 4) return DB_INT;
         else if (dataSize == 8) return DB_LONG;
         else return -1;
         break;
      case datatype::type::UINT:
         if (dataSize == 2) return DB_SHORT;
         else if (dataSize == 4) return DB_INT;
         else if (dataSize == 8) return DB_LONG;
         else return -1;
         break;
      case datatype::type::FLOAT:
         if (dataSize == 4) return DB_FLOAT;
         else if (dataSize == 8) return DB_DOUBLE;
         else return -1;
         break;
      case datatype::type::UNKNOWN:
         cerr << "INVALID DATATYPE AT " << __FILE__ << " " << __LINE__ << endl;
         exit(1);
   }
   return -1;
}

uint64_t convUInt(const char* ptr, const VLSV::datatype& dataType, const uint64_t& dataSize) {
   if (dataType != VLSV::UINT) {
      cerr << "Erroneous datatype given to convUInt" << endl;
      exit(1);
   }

   switch (dataSize) {
      case 1:
         return *reinterpret_cast<const unsigned char*> (ptr);
         break;
      case 2:
         return *reinterpret_cast<const unsigned short int*> (ptr);
         break;
      case 4:
         return *reinterpret_cast<const unsigned int*> (ptr);
         break;
      case 8:
         return *reinterpret_cast<const unsigned long int*> (ptr);
         break;
   }
   return 0;
}

uint64_t convUInt(const char* ptr, const datatype::type & dataType, const uint64_t& dataSize) {
   if (dataType != datatype::type::UINT) {
      cerr << "Erroneous datatype given to convUInt" << endl;
      exit(1);
   }

   switch (dataSize) {
      case 1:
         return *reinterpret_cast<const unsigned char*> (ptr);
         break;
      case 2:
         return *reinterpret_cast<const unsigned short int*> (ptr);
         break;
      case 4:
         return *reinterpret_cast<const unsigned int*> (ptr);
         break;
      case 8:
         return *reinterpret_cast<const unsigned long int*> (ptr);
         break;
   }
   return 0;
}


//Outputs the velocity block indices of some given block into indices
//Input:
//[0] cellStruct -- some cell structure that has been constructed properly
//[1] block -- some velocity block id
//Output:
//[0] indices -- the array where to store the indices
void getVelocityBlockCoordinates(const CellStructure & cellStruct, const uint64_t block, array<Real, 3> & coordinates ) {
   //First get indices:
   array<uint64_t, 3> blockIndices;
   blockIndices[0] = block % cellStruct.vcell_bounds[0];
   blockIndices[1] = (block / cellStruct.vcell_bounds[0]) % cellStruct.vcell_bounds[1];
   blockIndices[2] = block / (cellStruct.vcell_bounds[0] * cellStruct.vcell_bounds[1]);
   //Store the coordinates:
   for( int i = 0; i < 3; ++i ) {
      coordinates[i] = cellStruct.min_vcoordinates[i] + cellStruct.vblock_length[i] * blockIndices[i];
   }
   return;
}




template <class T>
bool convertVelocityBlockVariable(T& vlsvReader, const string& spatGridName, const string& velGridName,
        const uint64_t& N_blocks, const uint64_t& blockOffset, const string& varName) {
   bool success = true;
   list<pair<string, string> > attribs;
   attribs.push_back(make_pair("name", varName));
   attribs.push_back(make_pair("mesh", spatGridName));

   datatype::type dataType;
   uint64_t arraySize, vectorSize, dataSize;
   if (vlsvReader.getArrayInfo("BLOCKVARIABLE", attribs, arraySize, vectorSize, dataType, dataSize) == false) {
      cerr << "Could not read BLOCKVARIABLE array info" << endl;
      return false;
   }

   char* buffer = new char[N_blocks * vectorSize * dataSize];
   if (vlsvReader.readArray("BLOCKVARIABLE", attribs, blockOffset, N_blocks, buffer) == false) {
      cerr << "ERROR could not read block variable" << endl;
      delete[] buffer;
      return success;
   }

   string label = "Distrib.function";
   string unit = "1/m^3 (m/s)^3";
   int conserved = 1;
   int extensive = 1;
   DBoptlist* optList = DBMakeOptlist(4);
   DBAddOption(optList, DBOPT_LABEL, const_cast<char*> (label.c_str()));
   DBAddOption(optList, DBOPT_EXTENTS_SIZE, const_cast<char*> (unit.c_str()));
   DBAddOption(optList, DBOPT_CONSERVED, &conserved);
   DBAddOption(optList, DBOPT_EXTENSIVE, &extensive);
   DBPutUcdvar1(fileptr, varName.c_str(), velGridName.c_str(), buffer, N_blocks*vectorSize, NULL, 0, SiloType(dataType, dataSize), DB_ZONECENT, optList);

   DBFreeOptlist(optList);
   delete[] buffer;
   return success;
}

// Function for calculating the background and perturbed B sum
template <class T>
static void background_perturbed_B_sum( T & vlsvReader, const uint64_t B_dataSize, const uint64_t cellIndex, char * B_char ) {
   // vector size of magnetic field is always 3
   const uint B_vectorSize = 3;

   // background_B perturbed_B
   char * background_B = new char[B_dataSize*B_vectorSize]; // Datasize is 4 or 8 -  4 = B is written in floats -  8 = B is written in double
   const string background_name = "background_B";
   const uint vectorsToRead = 1;
   // Read 
   if(vlsvReader.readArray("VARIABLE", background_name, cellIndex, vectorsToRead, background_B) == false) {
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;exit(1);
   }

   const string perturbed_name = "perturbed_B";
   char * perturbed_B = new char[B_dataSize*B_vectorSize]; // Datasize is 4 or 8 -  4 = B is written in floats -  8 = B is written in double
   // Read
   if(vlsvReader.readArray("VARIABLE", perturbed_name, cellIndex, vectorsToRead, perturbed_B) == false) {
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;exit(1);
   }

   // Cast to float and double:
   float* B_float = reinterpret_cast<float*>(B_char);
   double* B_double = reinterpret_cast<double*>(B_char);
   float* background_B_float = reinterpret_cast<float*>(background_B);
   double* background_B_double = reinterpret_cast<double*>(background_B);
   float* perturbed_B_float = reinterpret_cast<float*>(perturbed_B);
   double* perturbed_B_double = reinterpret_cast<double*>(perturbed_B);

   // Do the sum:
   for( uint i = 0; i < B_vectorSize; ++i ) {
      if( B_dataSize == sizeof(float) ) {
         // It's a float
         B_float[i] = background_B_float[i] + perturbed_B_float[i];
      } else {
         // It's a double
         B_double[i] = background_B_double[i] + perturbed_B_double[i];
      }
   }
   // Free the memory:
   delete[] background_B;
   delete[] perturbed_B;
}


Real * getB( oldVlsv::Reader& vlsvReader, const string& meshName, const uint64_t& cellID ) {
   //Get B:
   //Declarations
   vlsv::datatype::type cellIdDataType;
   uint64_t cellIdArraySize, cellIdVectorSize, cellIdDataSize;
   //Output: cellIdArraySize, cellIdVectorSize, cellIdDataType, cellIdDatasize (inside if() because getArray is bool and returns false if something unexpected happens)
   list<pair<string, string>> xmlAttributes;
   xmlAttributes.push_back(make_pair("name", "SpatialGrid"));
   if (vlsvReader.getArrayInfo("MESH", xmlAttributes, cellIdArraySize, cellIdVectorSize, cellIdDataType, cellIdDataSize) == false) {
      cerr << "Error " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
      return NULL;
   }
   // Declare buffers and allocate memory, this is done to read in the cell id location:
   char * cellIdBuffer = new char[cellIdArraySize*cellIdVectorSize*cellIdDataSize];
   //read the array into cellIdBuffer starting from 0 up until cellIdArraySize which was received from getArrayInfo
   if (vlsvReader.readArray("MESH",meshName,0,cellIdArraySize,cellIdBuffer) == false) {
      //cerr << "Spatial cell #" << cellID << " not found!" << endl;
      cerr << "Error " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
      return NULL;
   }
   // Search for the given cellID location, the array in the vlsv file is not ordered depending on the cell id so the array might look like this, for instance: [CellId1, CellId7, CellId5, ...] and the variables are saved in the same order: [CellId1_B_FIELD, CellId7_B_FIELD, CellId5_B_FIELD, ...]
   uint64_t cellIndex = numeric_limits<uint64_t>::max();
   for (uint64_t cell = 0; cell < cellIdArraySize; ++cell) {
      //the CellID are not sorted in the array, so we'll have to search the array -- the CellID is stored in cellId
      const uint64_t readCellID = convUInt(cellIdBuffer + cell*cellIdDataSize, cellIdDataType, cellIdDataSize);
      if (cellID == readCellID) {
         //Found the right cell ID, break
         cellIndex = cell;
         break;
      }
   }

   // Check if the cell id was found:
   if (cellIndex == numeric_limits<uint64_t>::max()) {
      cerr << "Spatial cell #" << cellID << " not found! " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
      return NULL;
   }

   // The buffer for cell id is no longer needed since we have the cellIndex:
   delete[] cellIdBuffer;

   //These are needed to determine the buffer size.
   vlsv::datatype::type variableDataType;
   uint64_t variableArraySize, variableVectorSize, variableDataSize;

   // Read in B:
   
   //Input the B vector (or B_vol or background_b or perturbed_B depending on which of the arrays are in the vlsv file):
   vector<string> variableNames;
   variableNames.push_back("B");
   variableNames.push_back("B_vol");
   variableNames.push_back("background_B");
   string variableName;


   bool foundB = false;

   // Go through all the possible variations of B: note that we're only interested in the direction of B, not its magnitude.
   for( vector<string>::const_iterator it = variableNames.begin(); it != variableNames.end(); ++it ) {
      variableName = *it;
      //getArrayInfo output: variableArraysize, variableVectorSize, ...
      xmlAttributes.clear();
      xmlAttributes.push_back(make_pair("mesh", meshName));
      xmlAttributes.push_back(make_pair("name", variableName));

      if (vlsvReader.getArrayInfo("VARIABLE", xmlAttributes, variableArraySize, variableVectorSize, variableDataType, variableDataSize) == true) {
         // Found B:
         foundB = true;
         break;
      }
   }

   if( foundB == false ) {
      // Didn't find B for some reason
      cerr << "ERROR, FAILED TO LOAD VARIABLE B AT " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
      return NULL;
   }



   // Read B for the given cell id:
   const uint vectorsToRead = 1;
   char * B_char = new char[variableVectorSize * variableDataSize * vectorsToRead];

   // If the variable we read in is background_B we also need perturbed_B variables:
   if( variableName == "background_B" ) {
      background_perturbed_B_sum( vlsvReader, variableDataSize, cellIndex, B_char );
   } else {
      // Read the given cell id's value for B from the vlsv file and store into the B vector:
      if(vlsvReader.readArray("VARIABLE", variableName, cellIndex, vectorsToRead, B_char) == false) {
         cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;
         exit(1);
         return 0;
      }
   }

   // B might be either double or float 
   double * B_double = reinterpret_cast<double*>(B_char);
   float * B_float = reinterpret_cast<float*>(B_char);
   
   // Convert to Real (if necessary)
   Real * B;
   if( sizeof(Real) != variableDataSize ) {
      // Read-in data has a different size e.g. the variable we just read in is in float form and Real equals double
      B = new Real[variableVectorSize];
      for( uint i = 0; i < variableVectorSize; ++i ) {
         // Check the data size of the B vector in the vlsv file:
         if( sizeof(float) == variableDataSize ) {
            const Real value = B_float[i];
            B[i] = value;
         } else if( sizeof(double) == variableDataSize ) {
            const Real value = B_double[i];
            B[i] = value;
         } else {
            phiprof_assert( sizeof(double) != variableDataSize && sizeof(float) != variableDataSize );
         }
      }
      delete[] B_char;
   } else {
      // Don't need to convert:
      B = reinterpret_cast<Real*>(B_char);
   }
   //Return the B vector in Real* form
   return B;

}

array<Real, 3> getBulkVelocity( oldVlsv::Reader& vlsvReader, const string& meshName, const uint64_t& cellID ) {
   //Get bulk velocity:
   //Declarations
   vlsv::datatype::type meshDataType;
   uint64_t meshArraySize, meshVectorSize, meshDataSize;
   //Output: meshArraySize, meshVectorSize, meshDataType, meshDatasize (inside if() because getArray is bool and returns false if something unexpected happens)
   list<pair<string, string>> xmlAttributes;
   xmlAttributes.push_back(make_pair("name", "SpatialGrid"));
   if (vlsvReader.getArrayInfo("MESH", xmlAttributes, meshArraySize, meshVectorSize, meshDataType, meshDataSize) == false) {
      //cerr << "Spatial cell #" << cellID << " not found!" << endl;
      cerr << "Error " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   // Declare buffers and allocate memory
   char * meshBuffer = new char[meshArraySize*meshVectorSize*meshDataSize];
   //read the array into meshBuffer starting from 0 up until meshArraySize which was received from getArrayInfo
   if (vlsvReader.readArray("MESH",meshName,0,meshArraySize,meshBuffer) == false) {
      //cerr << "Spatial cell #" << cellID << " not found!" << endl;
      cerr << "Error " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   // Search for the given cellID:
   uint64_t cellIndex = numeric_limits<uint64_t>::max();
   for (uint64_t cell = 0; cell < meshArraySize; ++cell) {
      //the CellID are not sorted in the array, so we'll have to search the array -- the CellID is stored in mesh
      const uint64_t readCellID = convUInt(meshBuffer + cell*meshDataSize, meshDataType, meshDataSize);
      if (cellID == readCellID) {
         //Found the right cell ID, break
         cellIndex = cell;
         break;
      }
   }
   
   if (cellIndex == numeric_limits<uint64_t>::max()) {
      //cerr << "Spatial cell #" << cellID << " not found!" << endl;
      cerr << "Error " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   
   //These are needed to determine the buffer size.
   vlsv::datatype::type variableDataType;
   uint64_t variableArraySize, variableVectorSize, variableDataSize;
   bool foundSingleB = true;
   //getArrayInfo output: variableArraysize, variableVectorSize, ...
   xmlAttributes.clear();
   xmlAttributes.push_back(make_pair("mesh", meshName));
   // Get rho_v
   // FIXME handle the case where moments is available/rho_v is not available
   xmlAttributes.push_back(make_pair("name", "rho_v"));
   if (vlsvReader.getArrayInfo("VARIABLE", xmlAttributes, variableArraySize, variableVectorSize, variableDataType, variableDataSize) == false) {
      xmlAttributes.pop_back();
      
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   
   //Declare a buffer for reading the specific vector from the array
   char * rho_v_buffer = new char[variableVectorSize * variableDataSize];     //Needs to store vector times data size (Got that from getArrayInfo)
   Real * the_actual_buffer_ptr = reinterpret_cast<Real*>(rho_v_buffer);
   if( variableDataSize != sizeof(Real) ) {
      cerr << "ERROR, bad datasize at " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   
   //The corresponding vector is in the cellIndex we got from mesh -- we only need to read one vector -- that's why the '1' parameter
   const uint64_t numOfVecs = 1;
   //store the vector in the_actual_buffer buffer -- the data is extracted vector at a time
   if(vlsvReader.readArray("VARIABLE", "rho_v", cellIndex, numOfVecs, rho_v_buffer) == false) {
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   Real rhov[3];
   rhov[0] = the_actual_buffer_ptr[0];
   rhov[1] = the_actual_buffer_ptr[1];
   rhov[2] = the_actual_buffer_ptr[2];
   
   // Get rho
   xmlAttributes.pop_back();
   xmlAttributes.push_back(make_pair("name", "rho"));
   if (vlsvReader.getArrayInfo("VARIABLE", xmlAttributes, variableArraySize, variableVectorSize, variableDataType, variableDataSize) == false) {
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   //Declare a buffer for reading the specific vector from the array
   char * rho_buffer = new char[variableVectorSize * variableDataSize];     //Needs to store vector times data size (Got that from getArrayInfo)
   the_actual_buffer_ptr = reinterpret_cast<Real*>(rho_buffer);
   if( variableDataSize != sizeof(Real) ) {
      cerr << "ERROR, bad datasize at " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   
   //store the vector in the_actual_buffer buffer -- the data is extracted vector at a time
   if(vlsvReader.readArray("VARIABLE", "rho", cellIndex, numOfVecs, rho_buffer) == false) {
      cout << "ERROR " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   creal rho = *the_actual_buffer_ptr;
   
   array<Real, 3> bulkV;
   bulkV[0] = (rho == 0.0) ? 0.0 : rhov[0] / rho;
   bulkV[1] = (rho == 0.0) ? 0.0 : rhov[1] / rho;
   bulkV[2] = (rho == 0.0) ? 0.0 : rhov[2] / rho;
   return bulkV;
}

void doRotation(
  Real * vx_crds_rotated, Real * vy_crds_rotated, Real * vz_crds_rotated,
  const Real * vx_crds, const Real * vy_crds, const Real * vz_crds, 
  const Real * B, const unsigned int vec_size
) {
   //NOTE: assuming that B is a vector of size 3 and that _crds_rotated has been allocated
   //Using eigen3 library here.
   //Now we have the B vector, so now the idea is to rotate the v-coordinates so that B always faces z-direction
   //Since we're handling only one spatial cell, B is the same in every v-coordinate.
   const int _size = 3;

   Matrix<Real, _size, 1> _B(B[0], B[1], B[2]);
   Matrix<Real, _size, 1> unit_z(0, 0, 1);                    //Unit vector in z-direction
   Matrix<Real, _size, 1> Bxu = _B.cross( unit_z );        //Cross product of B and unit_z //Remove -1 if necessary -- just that I think it's the other way around
   //Check if divide by zero -- if there's division by zero, the B vector is already in the direction of z-axis and no need to do anything
   //Note: Bxu[2] is zero so it can be removed if need be but because of the loop later on it won't really make a difference in terms of performance
   if( (Bxu[0]*Bxu[0] + Bxu[1]*Bxu[1] + Bxu[2]*Bxu[2]) != 0 ) {
      //Determine the axis of rotation: (Note: Bxu[2] is zero)
      Matrix<Real, _size, 1> axisDir = Bxu/(sqrt(Bxu[0]*Bxu[0] + Bxu[1]*Bxu[1] + Bxu[2]*Bxu[2]));
      //Determine the angle of rotation: (No need for a check for div/by/zero because of the above check)
      Real rotAngle = acos(_B[2] / sqrt(_B[0]*_B[0] + _B[1]*_B[1] + _B[2]*_B[2])); //B_z / |B|
      //Determine the rotation matrix:
      Transform<Real, _size, _size> rotationMatrix( AngleAxis<Real>(rotAngle, axisDir) );
      for( unsigned int i = 0; i < vec_size; ++i ) {
         Matrix<Real, _size, 1> _v(vx_crds[i], vy_crds[i], vz_crds[i]);
         //Now we have the velocity vector. Let's rotate it in z-dir and save the rotated vec
         Matrix<Real, _size, 1> rotated_v = rotationMatrix*_v;
         //Save values:
         vx_crds_rotated[i] = rotated_v[0];
         vy_crds_rotated[i] = rotated_v[1];
         vz_crds_rotated[i] = rotated_v[2];
      }
   }
}

bool convertVelocityBlocks2(
   oldVlsv::Reader& vlsvReader,
   const string& fname,
   const string& meshName,
   const CellStructure &,
   const uint64_t& cellID,
   const bool rotate,
   const bool plasmaFrame
) {
   //return true;
   bool success = true;

   // Get some required info from VLSV file:
   // "cwb" = "cells with blocks" IDs of spatial cells which wrote their velocity grid
   // "nb"  = "number of blocks"  Number of velocity blocks in each velocity grid
   // "bc"  = "block coordinates" Coordinates of each block of each velocity grid
   list<pair<string, string> > attribs;


   vlsv::datatype::type cwb_dataType, nb_dataType, bc_dataType;
   uint64_t cwb_arraySize, cwb_vectorSize, cwb_dataSize;
   uint64_t nb_arraySize, nb_vectorSize, nb_dataSize;
   uint64_t bc_arraySize, bc_vectorSize, bc_dataSize;


   //read in number of blocks per cell
   attribs.clear();
   attribs.push_back(make_pair("name", meshName));
   if (vlsvReader.getArrayInfo("BLOCKSPERCELL", attribs, nb_arraySize, nb_vectorSize, nb_dataType, nb_dataSize) == false) {
      //cerr << "Could not find array CELLSWITHBLOCKS" << endl;
      return false;
   }

   // Create buffers for  number of blocks (nb) and read data:
   char* nb_buffer = new char[nb_arraySize * nb_vectorSize * nb_dataSize];
   if (vlsvReader.readArray("BLOCKSPERCELL", meshName, 0, nb_arraySize, nb_buffer) == false) success = false;
   if (success == false) {
      cerr << "Failed to read number of blocks for mesh '" << meshName << "'" << endl;
      delete nb_buffer;
      return success;
   }



   //read in other metadata
   attribs.clear();
   attribs.push_back(make_pair("name", meshName));
   if (vlsvReader.getArrayInfo("CELLSWITHBLOCKS", attribs, cwb_arraySize, cwb_vectorSize, cwb_dataType, cwb_dataSize) == false) {
      //cerr << "Could not find array CELLSWITHBLOCKS" << endl;
      return false;
   }

   // Create buffers for cwb,nb and read data:
   char* cwb_buffer = new char[cwb_arraySize * cwb_vectorSize * cwb_dataSize];
   if (vlsvReader.readArray("CELLSWITHBLOCKS", meshName, 0, cwb_arraySize, cwb_buffer) == false) success = false;
   if (success == false) {
      cerr << "Failed to read block metadata for mesh '" << meshName << "'" << endl;
      delete cwb_buffer;
      return success;
   }


   if (vlsvReader.getArrayInfo("BLOCKCOORDINATES", attribs, bc_arraySize, bc_vectorSize, bc_dataType, bc_dataSize) == false) {
      //cerr << "Could not find array BLOCKCOORDINATES" << endl;
      return false;
   }


   // Search for the given cellID:
   uint64_t blockOffset = 0;
   uint64_t cellIndex = numeric_limits<uint64_t>::max();
   uint64_t N_blocks;
   for (uint64_t cell = 0; cell < cwb_arraySize; ++cell) {
      const uint64_t readCellID = convUInt(cwb_buffer + cell*cwb_dataSize, cwb_dataType, cwb_dataSize);
      N_blocks = convUInt(nb_buffer + cell*nb_dataSize, nb_dataType, nb_dataSize);
      if (cellID == readCellID) {
         cellIndex = cell;
         break;
      }
      blockOffset += N_blocks;
   }
   if (cellIndex == numeric_limits<uint64_t>::max()) {
      //cerr << "Spatial cell #" << cellID << " not found!" << endl;
      return false;
   } else {
      //cout << "Spatial cell #" << cellID << " has offset " << blockOffset << endl;
   }

   map<NodeCrd<Real>, uint64_t, NodeComp> nodes;

   // Read all block coordinates of the velocity grid:
   char* bc_buffer = new char[N_blocks * bc_vectorSize * bc_dataSize];
   vlsvReader.readArray("BLOCKCOORDINATES", meshName, blockOffset, N_blocks, bc_buffer);
   for (uint64_t b = 0; b < N_blocks; ++b) {
      Real vx_min, vy_min, vz_min, dvx, dvy, dvz;
      if (bc_dataSize == 4) {
         vx_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 0 * bc_dataSize);
         vy_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 1 * bc_dataSize);
         vz_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 2 * bc_dataSize);
         dvx = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 3 * bc_dataSize);
         dvy = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 4 * bc_dataSize);
         dvz = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 5 * bc_dataSize);
      } else {
         vx_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 0 * bc_dataSize);
         vy_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 1 * bc_dataSize);
         vz_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 2 * bc_dataSize);
         dvx = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 3 * bc_dataSize);
         dvy = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 4 * bc_dataSize);
         dvz = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 5 * bc_dataSize);
      }

      creal EPS = 1.0e-7;
      for (int kv = 0; kv < 5; ++kv) {
         Real VZ = vz_min + kv*dvz;
         if (fabs(VZ) < EPS) VZ = 0.0;
         for (int jv = 0; jv < 5; ++jv) {
            Real VY = vy_min + jv*dvy;
            if (fabs(VY) < EPS) VY = 0.0;
            for (int iv = 0; iv < 5; ++iv) {
               Real VX = vx_min + iv*dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               nodes.insert(make_pair(NodeCrd<Real>(VX, VY, VZ), (uint64_t) 0));
            }
         }
      }
   }
   
   array<Real, 3> bulkV = {{0.0, 0.0, 0.0}};
   if(plasmaFrame) {
      bulkV = getBulkVelocity(vlsvReader, meshName, cellID);
   }
   
   Real* vx_crds = new Real[nodes.size()];
   Real* vy_crds = new Real[nodes.size()];
   Real* vz_crds = new Real[nodes.size()];
   const unsigned int _node_size = nodes.size();
   uint64_t counter = 0;
   for (map<NodeCrd<Real>, uint64_t>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
      it->second = counter;
      // bulkV contains the bulk velocity if plasmaFrame is true, otherwise it is 0.0
      vx_crds[counter] = it->first.x - bulkV[0];
      vy_crds[counter] = it->first.y - bulkV[1];
      vz_crds[counter] = it->first.z - bulkV[2];
      ++counter;
   }

   const uint64_t nodeListSize = N_blocks * 64 * 8;
   int* nodeList = new int[nodeListSize];
   for (uint64_t b = 0; b < N_blocks; ++b) {
      Real vx_min, vy_min, vz_min, dvx, dvy, dvz;
      if (bc_dataSize == 4) {
         // floats
         vx_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 0 * sizeof (float));
         vy_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 1 * sizeof (float));
         vz_min = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 2 * sizeof (float));
         dvx = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 3 * sizeof (float));
         dvy = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 4 * sizeof (float));
         dvz = *reinterpret_cast<float*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 5 * sizeof (float));
      } else {
         // doubles
         vx_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 0 * sizeof (double));
         vy_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 1 * sizeof (double));
         vz_min = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 2 * sizeof (double));
         dvx = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 3 * sizeof (double));
         dvy = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 4 * sizeof (double));
         dvz = *reinterpret_cast<double*> (bc_buffer + b * bc_vectorSize * bc_dataSize + 5 * sizeof (double));
      }
      Real VX, VY, VZ;
      creal EPS = 1.0e-7;
      map<NodeCrd<Real>, uint64_t>::const_iterator it;
      for (int kv = 0; kv < 4; ++kv) {
         for (int jv = 0; jv < 4; ++jv) {
            for (int iv = 0; iv < 4; ++iv) {
               const unsigned int cellInd = kv * 16 + jv * 4 + iv;
               VX = vx_min + iv*dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               VY = vy_min + jv*dvy;
               if (fabs(VY) < EPS) VY = 0.0;
               VZ = vz_min + kv*dvz;
               if (fabs(VZ) < EPS) VZ = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 0] = it->second;

               VX = vx_min + (iv + 1) * dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 1] = it->second;

               VY = vy_min + (jv + 1) * dvy;
               if (fabs(VY) < EPS) VY = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 2] = it->second;

               VX = vx_min + iv*dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 3] = it->second;

               VY = vy_min + jv*dvy;
               if (fabs(VY) < EPS) VY = 0.0;
               VZ = vz_min + (kv + 1) * dvz;
               if (fabs(VZ) < EPS) VZ = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 4] = it->second;

               VX = vx_min + (iv + 1) * dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 5] = it->second;

               VY = vy_min + (jv + 1) * dvy;
               if (fabs(VY) < EPS) VY = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 6] = it->second;

               VX = vx_min + iv*dvx;
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY + dvy << ' ' << VZ + dvz << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 7] = it->second;
            }
         }
      }
   }

   const int N_dims = 3; // Number of dimensions
   const int N_nodes = nodes.size(); // Total number of nodes
   const int N_zones = N_blocks * 64; // Total number of zones (=velocity cells)
   int shapeTypes[] = {DB_ZONETYPE_HEX}; // Hexahedrons only
   int shapeSizes[] = {8}; // Each hexahedron has 8 nodes
   int shapeCnt[] = {N_zones}; // Only 1 shape type (hexahedron)
   const int N_shapes = 1; //  -- "" --

   void* coords[3]; // Pointers to coordinate arrays
   Real * vx_crds_rotated = new Real[_node_size];
   Real * vy_crds_rotated = new Real[_node_size];
   Real * vz_crds_rotated = new Real[_node_size];
   if( !rotate ) {
      coords[0] = vx_crds;
      coords[1] = vy_crds;
      coords[2] = vz_crds;
   } else {
      //rotate == true, do the rotation
      Real * B_ptr = getB( vlsvReader, meshName, cellID ); //Note: allocates memory and stores the vector value into B_ptr
      

      //Now rotate:
      //Using eigen3 library here.
      //Now we have the B vector, so now the idea is to rotate the v-coordinates so that B always faces z-direction
      //Since we're handling only one spatial cell, B is the same in every v-coordinate.

      //Rotate the v-coordinates and store them in vx_crds_rotated, vy_crds_rotated, ... :
      doRotation( vx_crds_rotated, vy_crds_rotated, vz_crds_rotated, vx_crds, vy_crds, vz_crds, B_ptr, _node_size );

      coords[0] = vx_crds_rotated;
      coords[1] = vy_crds_rotated;
      coords[2] = vz_crds_rotated;
   }





   // Write zone list into silo file:
   stringstream ss;
   ss << "VelGrid";
   const string zoneListName = ss.str() + "Zones";
   if (DBPutZonelist2(fileptr, zoneListName.c_str(), N_zones, N_dims, nodeList, nodeListSize, 0, 0, 0, shapeTypes, shapeSizes, shapeCnt, N_shapes, NULL) < 0) success = false;

   // Write grid into silo file:
   const string gridName = ss.str();
   if (DBPutUcdmesh(fileptr, gridName.c_str(), N_dims, NULL, coords, N_nodes, N_zones, zoneListName.c_str(), NULL, SiloType(bc_dataType, bc_dataSize), NULL) < 0) success = false;

   delete nodeList;
   delete vx_crds;
   delete vy_crds;
   delete vz_crds;
   delete bc_buffer;
   delete[] vx_crds_rotated;
   delete[] vy_crds_rotated;
   delete[] vz_crds_rotated;

   list<string> blockVarNames;
   if (vlsvReader.getBlockVariableNames(meshName, blockVarNames) == false) {
      cerr << "Failed to read block variable names!" << endl;
      success = false;
   }
   if (success == true) {
      for (list<string>::iterator it = blockVarNames.begin(); it != blockVarNames.end(); ++it) {
         if (convertVelocityBlockVariable(vlsvReader, meshName, gridName, N_blocks, blockOffset, *it) == false) success = false;
      }
   }

   return success;
}

bool convertSlicedVelocityMesh(oldVlsv::Reader& vlsvReader,const string& fname,const string& meshName,
                               CellStructure& cellStruct) {
   cerr << "wrong vlsv" << endl; return false;
}

bool convertSlicedVelocityMesh(newVlsv::Reader& vlsvReader,const string& fname,const string& meshName,
                               CellStructure& cellStruct) {
   bool success = true;
   
   // TEST
   cellStruct.slicedCoords[0] = 2;
   cellStruct.slicedCoords[1] = 4;
   cellStruct.slicedCoords[2] = 5;
   cellStruct.slicedCoordValues[0] = 1e3;
   cellStruct.slicedCoordValues[1] = 5e3;
   cellStruct.slicedCoordValues[2] = 5e3;

   string outputMeshName = "VelSlice";
   vlsv::Writer out;
   if (out.open(fname,MPI_COMM_SELF,0) == false) {
      cerr << "ERROR, failed to open output file with vlsv::Writer at " << __FILE__ << " " << __LINE__ << endl;
      return false;
   }

   vector<uint64_t> cellIDs;
   if (vlsvReader.getCellIds(cellIDs) == false) {
      cerr << "ERROR: failed to get cell IDs in " << __FILE__ << ' ' << __LINE__ << endl;
      return false;
   }

   uint64_t bbox[6]; // Number of cells in the generated mesh
   int dims[3];      // Output dimensions
   if (cellStruct.slicedCoords[0] == 0) {
      dims[0] = 1; dims[1] = 2;
      bbox[0] = cellStruct.cell_bounds[1]; bbox[1] = cellStruct.cell_bounds[2];
   } else if (cellStruct.slicedCoords[0] == 1) {
      dims[0] = 0; dims[1] = 2;
      bbox[0] = cellStruct.cell_bounds[0]; bbox[1] = cellStruct.cell_bounds[2];
   } else {
      dims[0] = 0; dims[1] = 1;
      bbox[0] = cellStruct.cell_bounds[0]; bbox[1] = cellStruct.cell_bounds[2];
   }
   bbox[3]=1; bbox[4]=1; bbox[5]=4;

   dims[2]=3;
   if (cellStruct.slicedCoords[1] == 3) dims[2] = 4;
   if (cellStruct.slicedCoords[2] == 4) dims[2] = 5;

   if (vlsv::initMesh(cellStruct.vcell_bounds[0],cellStruct.vcell_bounds[0],cellStruct.vcell_bounds[0],cellStruct.maxVelRefLevel) == false) {
      cerr << "ERROR: failed to init AMR mesh in " << __FILE__ << ' ' << __LINE__ << endl;
      return false;
   }

   // Get the names of velocity mesh variables
   set<string> blockVarNames;
   const string attributeName = "name";
   if (vlsvReader.getUniqueAttributeValues("BLOCKVARIABLE",attributeName,blockVarNames) == false) {
      cerr << "ERROR, FAILED TO GET UNIQUE ATTRIBUTE VALUES AT " << __FILE__ << " " << __LINE__ << endl;
   }

   struct BlockVarInfo {
      string name;
      vlsv::datatype::type dataType;
      uint64_t vectorSize;
      uint64_t dataSize;
   };
   vector<BlockVarInfo> varInfo;

   // Assume that two of the coordinates are spatial, i.e., that the first 
   // sliced coordinate is a spatial one
   vector<float> nodeCoords;
   vector<int> connectivity;
   vector<vector<char> > variables;
   for (size_t cell=0; cell<cellIDs.size(); ++cell) {
      uint64_t cellId = cellIDs[cell]-1;
      uint64_t cellIndices[3];
      cellIndices[0] = cellId % cellStruct.cell_bounds[0];
      cellIndices[1] = ((cellId - cellIndices[0]) / cellStruct.cell_bounds[0]) % cellStruct.cell_bounds[1];
      cellIndices[2] = ((cellId - cellStruct.cell_bounds[0]*cellIndices[1]) / (cellStruct.cell_bounds[0]*cellStruct.cell_bounds[1]));

      // Calculate cell coordinates and check if the sliced spatial coordinate is in it
      Real cellCrds[6];
      for (int i=0; i<3; ++i) cellCrds[i  ] = cellStruct.min_coordinates[i] +   cellIndices[i]  * cellStruct.cell_length[i];
      for (int i=0; i<3; ++i) cellCrds[i+3] = cellStruct.min_coordinates[i] + (cellIndices[i]+1)* cellStruct.cell_length[i];

      if (cellCrds[cellStruct.slicedCoords[0]] > cellStruct.slicedCoordValues[0]) continue;
      if (cellCrds[cellStruct.slicedCoords[0]+3] < cellStruct.slicedCoordValues[0]) continue;

      // Buffer all velocity mesh variables
      vector<char*> varBuffer(blockVarNames.size());
      int counter=0;
      for (set<string>::const_iterator var=blockVarNames.begin(); var!=blockVarNames.end(); ++var) {
         varBuffer[counter] = NULL;
         if (vlsvReader.getVelocityBlockVariables(*var,cellIDs[cell],varBuffer[counter],true) == false) {
            varBuffer[counter] = NULL;
         }
         ++counter;
      }

      // Store block variable info, we need this to write the variable data
      varInfo.clear();
      for (set<string>::const_iterator var=blockVarNames.begin(); var!=blockVarNames.end(); ++var) {
         list<pair<string,string> > attribs;
         attribs.push_back(make_pair("name",*var));
         attribs.push_back(make_pair("mesh",meshName));
         uint64_t arraySize;
         BlockVarInfo vinfo;
         vinfo.name = *var;
         if (vlsvReader.getArrayInfo("BLOCKVARIABLE",attribs,arraySize,vinfo.vectorSize,vinfo.dataType,vinfo.dataSize) == false) {
            cerr << "Could not read BLOCKVARIABLE array info" << endl;
         }
         varInfo.push_back(vinfo);
      }
      if (varInfo.size() > variables.size()) variables.resize(varInfo.size());

      vector<uint64_t> blockIDs;
      if (vlsvReader.getBlockIds(cellIDs[cell],blockIDs) == false) {         
         for (size_t v=0; v<varBuffer.size(); ++v) delete [] varBuffer[v];
         continue;
      }

      counter=0;
      for (size_t b=0; b<blockIDs.size(); ++b) {
         uint64_t blockGID = blockIDs[b];

         // Figure out block indices and refinement level
         uint32_t refLevel;
         uint32_t blockIndices[3];
         vlsv::calculateCellIndices(blockGID,refLevel,blockIndices[0],blockIndices[1],blockIndices[2]);
         uint32_t refMul  = pow(2,refLevel);
         uint32_t refDiff = pow(2,cellStruct.maxVelRefLevel-refLevel);

         // Calculate block coordinates
         Real minBlockCoords[3];
         Real maxBlockCoords[3];
         for (int i=0; i<3; ++i) {
            minBlockCoords[i] = cellStruct.min_vcoordinates[i] + blockIndices[i]*cellStruct.vblock_length[i]/refMul;
            maxBlockCoords[i] = cellStruct.min_vcoordinates[i] + (blockIndices[i]+1)*cellStruct.vblock_length[i]/refMul;
         }

         // If the chosen velocity coordinates are in the block, store
         // the relevant cell (with correct size in velocity direction)
         if (cellStruct.slicedCoordValues[1] < minBlockCoords[cellStruct.slicedCoords[1]-3]
             || cellStruct.slicedCoordValues[1] > maxBlockCoords[cellStruct.slicedCoords[1]-3]) continue;
         if (cellStruct.slicedCoordValues[2] < minBlockCoords[cellStruct.slicedCoords[2]-3]
             || cellStruct.slicedCoordValues[2] > maxBlockCoords[cellStruct.slicedCoords[2]-3]) continue;
         
         // Store node coordinates and cell connectivity entries for the accepted cells
         const Real DV_cell = cellStruct.vblock_length[dims[2]-3]/refMul/4;
         for (int i=0; i<4; ++i) {
            const size_t offset = nodeCoords.size()/3;
            nodeCoords.push_back(cellCrds[dims[0]  ]); nodeCoords.push_back(cellCrds[dims[1]  ]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+i*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]+3]); nodeCoords.push_back(cellCrds[dims[1]  ]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+i*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]  ]); nodeCoords.push_back(cellCrds[dims[1]+3]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+i*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]+3]); nodeCoords.push_back(cellCrds[dims[1]+3]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+i*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]  ]); nodeCoords.push_back(cellCrds[dims[1]  ]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+(i+1)*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]+3]); nodeCoords.push_back(cellCrds[dims[1]  ]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+(i+1)*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]  ]); nodeCoords.push_back(cellCrds[dims[1]+3]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+(i+1)*DV_cell);
            nodeCoords.push_back(cellCrds[dims[0]+3]); nodeCoords.push_back(cellCrds[dims[1]+3]); nodeCoords.push_back(minBlockCoords[dims[2]-3]+(i+1)*DV_cell);

            connectivity.push_back(vlsv::celltype::VOXEL);
            connectivity.push_back(8);
            for (int j=0; j<8; ++j) connectivity.push_back(offset+j);
         }

         // Store value of distribution function in saved cells
         const Real DVY_CELL = cellStruct.vblock_length[cellStruct.slicedCoords[1]-3]/refMul;
         const Real DVZ_CELL = cellStruct.vblock_length[cellStruct.slicedCoords[2]-3]/refMul;
         int j = static_cast<int>((cellStruct.slicedCoordValues[1] - minBlockCoords[cellStruct.slicedCoords[1]-3]) / DVY_CELL);
         int k = static_cast<int>((cellStruct.slicedCoordValues[2] - minBlockCoords[cellStruct.slicedCoords[2]-3]) / DVY_CELL);

         for (size_t v=0; v<varBuffer.size(); ++v) {
            uint64_t entrySize = varInfo[v].vectorSize*varInfo[v].dataSize;
            char* baseptr = &(varBuffer[v][0]) + b*entrySize;

            for (int i=0; i<4; ++i) {
               char* varptr = baseptr + i*varInfo[v].dataSize;
               int index;
               switch (dims[2]) {
                case 3:
                  for (uint64_t dummy=0; dummy<varInfo[v].dataSize; ++dummy) variables[v].push_back(varptr[dummy]);
                  break;
                case 4:
                  for (uint64_t dummy=0; dummy<varInfo[v].dataSize; ++dummy) variables[v].push_back(varptr[dummy]);
                  break;
                case 5:
                  for (uint64_t dummy=0; dummy<varInfo[v].dataSize; ++dummy) variables[v].push_back(varptr[dummy]);
                  break;
               }
            }
         }
         ++counter;
      }

      for (size_t v=0; v<varBuffer.size(); ++v) delete [] varBuffer[v];
   }

   map<string,string> attributes;
   attributes["name"] = outputMeshName;
   attributes["type"] = vlsv::mesh::STRING_UCD_GENERIC_MULTI;
   attributes["domains"] = "1";
   attributes["cells"] = connectivity.size()/10;
   attributes["nodes"] = nodeCoords.size()/3;

   if (out.writeArray("MESH",attributes,connectivity.size(),1,&(connectivity[0])) == false) success = false;

   attributes.clear();
   attributes["mesh"] = outputMeshName;   
   if (out.writeArray("MESH_NODE_CRDS",attributes,nodeCoords.size()/3,3,&(nodeCoords[0])) == false) success = false;
   
   bbox[0]=1; bbox[1]=1; bbox[2]=1; bbox[3]=1; bbox[4]=1; bbox[5]=1;
   if (out.writeArray("MESH_BBOX",attributes,6,1,bbox) == false) success = false;
   
   uint32_t offsetEntries[vlsv::ucdgenericmulti::offsets::SIZE];
   offsetEntries[vlsv::ucdgenericmulti::offsets::ZONE_ENTRIES] = connectivity.size();
   offsetEntries[vlsv::ucdgenericmulti::offsets::NODE_ENTRIES] = nodeCoords.size()/3;
   if (out.writeArray("MESH_OFFSETS",attributes,1,vlsv::ucdgenericmulti::offsets::SIZE,offsetEntries) == false) success=false;

   uint32_t domainSize[vlsv::ucdgenericmulti::domainsizes::SIZE];
   domainSize[vlsv::ucdgenericmulti::domainsizes::TOTAL_BLOCKS] = connectivity.size()/10;
   domainSize[vlsv::ucdgenericmulti::domainsizes::GHOST_BLOCKS] = 0;
   domainSize[vlsv::ucdgenericmulti::domainsizes::TOTAL_NODES] = nodeCoords.size()/3;
   domainSize[vlsv::ucdgenericmulti::domainsizes::GHOST_NODES] = 0;
   if (out.writeArray("MESH_DOMAIN_SIZES",attributes,1,vlsv::ucdgenericmulti::domainsizes::SIZE,domainSize) == false) success = false;

   for (size_t v=0; v<variables.size(); ++v) {
      uint64_t vectorSize = varInfo[v].vectorSize/64;
      uint64_t entrySize = vectorSize*varInfo[v].dataSize;
      uint64_t arraySize = variables[v].size() / entrySize;
      if (variables[v].size() % entrySize != 0) {
         cerr << "Error in variable array size in " << __FILE__ << ' ' << __LINE__ << endl;
         continue;
      }

      attributes["name"] = varInfo[v].name;
      char* ptr = reinterpret_cast<char*>(&(variables[v][0]));
      if (out.writeArray("VARIABLE",attributes,vlsv::getStringDatatype(varInfo[v].dataType),arraySize,vectorSize,varInfo[v].dataSize,ptr) == false) {
         cerr << "Failed to write variable '" << varInfo[v].name << "' to sliced velocity mesh" << endl;
      }
   }

   out.close();
   return success;
}

bool convertVelocityBlocks2(
                            newVlsv::Reader& vlsvReader,
                            const string& fname,
                            const string& meshName,
                            const CellStructure& cellStruct,
                            const uint64_t& cellID,
                            const bool rotate,
                            const bool plasmaFrame
                           ) {
   bool success = true;
   
   string outputMeshName = "VelGrid";
   int cellsInBlocksPerDirection = 4;

   vlsv::Writer out;
   if (out.open(fname,MPI_COMM_SELF,0) == false) {
      cerr << "ERROR, failed to open output file with vlsv::Writer at " << __FILE__ << " " << __LINE__ << endl;
      return false;
   }
   
   // Read velocity block global IDs and write them out
   vector<uint64_t> blockIds;
   if (vlsvReader.getBlockIds(cellID,blockIds) == false ) {
      cerr << "ERROR, FAILED TO READ BLOCK IDS AT " << __FILE__ << " " << __LINE__ << endl;
      return false;
   }
   const size_t N_blocks = blockIds.size();
   
   map<string,string> attributes;
   attributes["name"] = outputMeshName;
   attributes["type"] = vlsv::mesh::STRING_UCD_AMR;
   stringstream ss;
   ss << (uint32_t)cellStruct.maxVelRefLevel;
   attributes["max_refinement_level"] = ss.str();
   attributes["geometry"] = vlsv::geometry::STRING_CARTESIAN;
   if (out.writeArray("MESH",attributes,blockIds.size(),1,&(blockIds[0])) == false) success = false;
   
   attributes["name"] = "VelBlocks";
   if (out.writeArray("MESH",attributes,blockIds.size(),1,&(blockIds[0])) == false) success = false;
   
   attributes.clear();

   // Make domain size array
   uint64_t domainSize[2];
   domainSize[0] = blockIds.size();
   domainSize[1] = 0;
   attributes["mesh"] = outputMeshName;
   if (out.writeArray("MESH_DOMAIN_SIZES",attributes,1,2,domainSize) == false) success = false;
     {
        vector<uint64_t> ().swap(blockIds);
     }
   
   attributes["mesh"] = "VelBlocks";
   if (out.writeArray("MESH_DOMAIN_SIZES",attributes,1,2,domainSize) == false) success = false;

   // Make bounding box array
   uint64_t bbox[6];
   bbox[0] = cellStruct.vcell_bounds[0];
   bbox[1] = cellStruct.vcell_bounds[1];
   bbox[2] = cellStruct.vcell_bounds[2];
   
   bbox[3] = 1;
   bbox[4] = 1;
   bbox[5] = 1;
   attributes["mesh"] = "VelBlocks";
   if (out.writeArray("MESH_BBOX",attributes,6,1,bbox) == false) success = false;
   
   bbox[3] = cellsInBlocksPerDirection;
   bbox[4] = cellsInBlocksPerDirection;
   bbox[5] = cellsInBlocksPerDirection;
   const uint32_t blockSize = bbox[3]*bbox[4]*bbox[5];
   attributes["mesh"] = outputMeshName;
   if (out.writeArray("MESH_BBOX",attributes,6,1,bbox) == false) success = false;

   // Make node coordinate arrays
   vector<float> coords;
   for (int crd=0; crd<3; ++crd) {
      // crd enumerates the coordinate: 0 = vx, 1 = vy, 2 = vz
      coords.clear();

      // Generate node coordinates
      for (size_t i=0; i<bbox[crd]; ++i) {
         for (size_t j=0; j<bbox[crd+3]; ++j) {
            coords.push_back( cellStruct.min_vcoordinates[crd] + i*cellStruct.vblock_length[crd] + j*cellStruct.vblock_length[crd]/bbox[crd+3] );
         }
      }
      coords.push_back( cellStruct.min_vcoordinates[crd] + bbox[crd]*cellStruct.vblock_length[crd] );

      // Write them to output file
      string arrayName;
      if (crd == 0) arrayName = "MESH_NODE_CRDS_X";
      else if (crd == 1) arrayName = "MESH_NODE_CRDS_Y";
      else if (crd == 2) arrayName = "MESH_NODE_CRDS_Z";
      
      if (coords.size() != bbox[crd]*bbox[crd+3]+1) {
         cerr << "ERROR incorrect node coordinates at " << __FILE__ << " " << __LINE__ << endl;
      }

      attributes["mesh"] = outputMeshName;
      if (out.writeArray(arrayName,attributes,coords.size(),1,&(coords[0])) == false) success = false;
   }
     {
        vector<float> ().swap(coords);
     }
   
   for (int crd=0; crd<3; ++crd) {
      coords.clear();
      for (size_t i=0; i<bbox[crd]; ++i) {
         coords.push_back( cellStruct.min_vcoordinates[crd] + i*cellStruct.vblock_length[crd] );
      }
      coords.push_back( cellStruct.min_vcoordinates[crd] + bbox[crd]*cellStruct.vblock_length[crd] );
      
      string arrayName;
      if (crd == 0) arrayName = "MESH_NODE_CRDS_X";
      else if (crd == 1) arrayName = "MESH_NODE_CRDS_Y";
      else if (crd == 2) arrayName = "MESH_NODE_CRDS_Z";
      
      attributes["mesh"] = "VelBlocks";
      if (out.writeArray(arrayName,attributes,coords.size(),1,&(coords[0])) == false) success = false;
   }
     {
        vector<float> ().swap(coords);
     }
   
   // Write dummy ghost zone data (not applicable here):
   uint64_t dummy;
   attributes["mesh"] = outputMeshName;
   if (out.writeArray("MESH_GHOST_LOCALIDS",attributes,domainSize[1],1,&dummy) == false) success = false;
   if (out.writeArray("MESH_GHOST_DOMAINS",attributes,domainSize[1],1,&dummy) == false) success = false;
   
   attributes["mesh"] = "VelBlocks";
   if (out.writeArray("MESH_GHOST_LOCALIDS",attributes,domainSize[1],1,&dummy) == false) success = false;
   if (out.writeArray("MESH_GHOST_DOMAINS",attributes,domainSize[1],1,&dummy) == false) success = false;

   // ***** Convert variables ***** //
   
   //Get the name of variables:
   set<string> blockVarNames;
   const string attributeName = "name";
   if (vlsvReader.getUniqueAttributeValues( "BLOCKVARIABLE", attributeName, blockVarNames) == false) {
      cerr << "ERROR, FAILED TO GET UNIQUE ATTRIBUTE VALUES AT " << __FILE__ << " " << __LINE__ << endl;
   }
   
   //Writing SILO file
   if (success == true) {
      for (set<string>::iterator it = blockVarNames.begin(); it != blockVarNames.end(); ++it) {
         list<pair<string, string> > attribs;
         attribs.push_back(make_pair("name", *it));
         attribs.push_back(make_pair("mesh", meshName));
         datatype::type dataType;
         uint64_t arraySize, vectorSize, dataSize;
         if (vlsvReader.getArrayInfo("BLOCKVARIABLE", attribs, arraySize, vectorSize, dataType, dataSize) == false) {
            cerr << "Could not read BLOCKVARIABLE array info" << endl;
            return false;
         }
	 
         char* buffer = new char[N_blocks * vectorSize * dataSize];
         if (vlsvReader.readArray("BLOCKVARIABLE", attribs, vlsvReader.getBlockOffset(cellID), N_blocks, buffer) == false) {
            cerr << "ERROR could not read block variable" << endl;
            delete[] buffer;
            return success;
         }

         attributes["name"] = *it;
         attributes["mesh"] = outputMeshName;
         if (out.writeArray("VARIABLE",attributes,vlsv::getStringDatatype(dataType),arraySize*blockSize,vectorSize/blockSize,dataSize,buffer) == false) success = false;
         
         delete [] buffer; buffer = NULL;
      }
   }
   
   vlsvReader.clearCellsWithBlocks();
   out.close();
   return success;
   
   
/*

   //return true;
   bool success = true;

   //Map for holding nodes:
   map<NodeCrd<Real>, uint64_t, NodeComp> nodes;

//   //Read cells with blocks into vlsvReader:
//   if( vlsvReader.setCellsWithBlocks() == false ) {
//      cerr << "ERROR, FAILED TO SET CELLS WITH BLOCKS AT " << __FILE__ << " " << __LINE__ << endl;
//      return false;
//   }
   //Read block ids:
   vector<uint64_t> blockIds;
   if( vlsvReader.getBlockIds( cellID, blockIds ) == false ) {
      cerr << "ERROR, FAILED TO READ BLOCK IDS AT " << __FILE__ << " " << __LINE__ << endl;
      return false;
   }
   //Clear cells with blocks (Calls clear on an unordered map inside vlsvReader)
   //vlsvReader.clearCellsWithBlocks();

   for (vector<uint64_t>::const_iterator it = blockIds.begin(); it != blockIds.end(); ++it) {
      const uint64_t & blockId = *it;

      //Get the block's coordinates:
      array<Real, 3> blockCoordinates;
      getVelocityBlockCoordinates( cellStruct, blockId, blockCoordinates );

      const unsigned short int numberOfCoordinates = 3;

      const uint16_t cellsInBlocksPerDirection = 4;
      //Get the length of a velocity cell
      Real vcell_length[numberOfCoordinates];
      for( int j = 0; j < numberOfCoordinates; ++j ) {
         vcell_length[j] = cellStruct.vblock_length[j] / cellsInBlocksPerDirection;
      }

      //Insert coordinates into nodes:
      creal EPS = 1.0e-7;
      const uint16_t numberOfNodesPerDirectionPerBlock = 5;
      for (int kv = 0; kv < numberOfNodesPerDirectionPerBlock; ++kv) {
         Real VZ = blockCoordinates[2] + kv*vcell_length[2];
         if (fabs(VZ) < EPS) VZ = 0.0;
         for (int jv = 0; jv < numberOfNodesPerDirectionPerBlock; ++jv) {
            Real VY = blockCoordinates[1] + jv*vcell_length[1];
            if (fabs(VY) < EPS) VY = 0.0;
            for (int iv = 0; iv < numberOfNodesPerDirectionPerBlock; ++iv) {
               Real VX = blockCoordinates[0] + iv*vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               nodes.insert(make_pair(NodeCrd<Real>(VX, VY, VZ), (uint64_t) 0));
            }
         }
      }
   }
   
   //Get bulk velocity vector
   array<Real, 3> vect3 = {{0.0, 0.0, 0.0}};
   array<Real, 4> vect4 = {{0.0, 0.0, 0.0, 0.0}};
   array<Real, 1> rho = {{0.0}};
   // if plasmaFrame is true then we get the bulk velocity otherwise it remains 0.0
   if(plasmaFrame) {
      if( vlsvReader.setCellIds() == false ) {
         cerr << "ERROR, FAILED TO SET CELL IDS AT " << __FILE__ << " " << __LINE__ << endl;
         return false;
      }
      //Input the bulk velocity vector:
      string variableName1 = "rho_v";
      string variableName2 = "rho";
      if( (vlsvReader.getVariable( variableName1, cellID, vect3 ) && vlsvReader.getVariable( variableName2, cellID, rho )) == false ) {
         variableName1 = "moments";
         if( vlsvReader.getVariable( variableName1, cellID, vect4 ) == false ) {
            cerr << "ERROR, FAILED TO LOAD VARIABLES rho_v, rho or moments AT " << __FILE__ << " " << __LINE__ << endl;
            return false;
         } else {
            if(vect4[0] != 0.0) {
               vect3[0] = vect4[1] / vect4[0]; // bulk velocity x
               vect3[1] = vect4[2] / vect4[0]; // bulk velocity y
               vect3[2] = vect4[3] / vect4[0]; // bulk velocity z
            } else {
               vect3[0] = 0.0;
               vect3[1] = 0.0;
               vect3[2] = 0.0;
            }
         }
      } else {
         if(rho[0] != 0.0) {
            vect3[0] /= rho[0]; // bulk velocity x
            vect3[1] /= rho[0]; // bulk velocity y
            vect3[2] /= rho[0]; // bulk velocity z
         } else {
            vect3[0] = 0.0;
            vect3[1] = 0.0;
            vect3[2] = 0.0;
         }
      }
   }

   Real* vx_crds = new Real[nodes.size()];
   Real* vy_crds = new Real[nodes.size()];
   Real* vz_crds = new Real[nodes.size()];
   const unsigned int _node_size = nodes.size();
   uint64_t counter = 0;
   for (map<NodeCrd<Real>, uint64_t>::iterator it = nodes.begin(); it != nodes.end(); ++it) {
      it->second = counter;
      // vect3 contains the bulk velocity if plasmaFrame is true, otherwise it is 0.0
      vx_crds[counter] = it->first.x - vect3[0];
      vy_crds[counter] = it->first.y - vect3[1];
      vz_crds[counter] = it->first.z - vect3[2];
      ++counter;
   }

   const uint64_t nodeListSize = blockIds.size() * 64 * 8;
   int* nodeList = new int[nodeListSize];
   //Iterate through blocks
   uint b = 0;
   for (vector<uint64_t>::const_iterator i = blockIds.begin(); i != blockIds.end(); ++i, ++b) {
      const uint64_t & blockId = *i;

      //Get the block's coordinates:
      array<Real, 3> blockCoordinates;
      getVelocityBlockCoordinates( cellStruct, blockId, blockCoordinates );

      const unsigned short int numberOfCoordinates = 3;
      const uint16_t cellsInBlocksPerDirection = 4;

      //Get the length of a velocity cell
      Real vcell_length[numberOfCoordinates];
      for( int j = 0; j < numberOfCoordinates; ++j ) {
         vcell_length[j] = cellStruct.vblock_length[j] / cellsInBlocksPerDirection;
      }

      //Input velocity cell coordinates:
      Real VX, VY, VZ;
      creal EPS = 1.0e-7;
      map<NodeCrd<Real>, uint64_t>::const_iterator it;
      for (int kv = 0; kv < 4; ++kv) {
         for (int jv = 0; jv < 4; ++jv) {
            for (int iv = 0; iv < 4; ++iv) {
               const unsigned int cellInd = kv * 16 + jv * 4 + iv;
               VX = blockCoordinates[0] + iv*vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               VY = blockCoordinates[1] + jv*vcell_length[1];
               if (fabs(VY) < EPS) VY = 0.0;
               VZ = blockCoordinates[2] + kv*vcell_length[2];
               if (fabs(VZ) < EPS) VZ = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 0] = it->second;

               VX = blockCoordinates[0] + (iv + 1) * vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 1] = it->second;

               VY = blockCoordinates[1] + (jv + 1) * vcell_length[1];
               if (fabs(VY) < EPS) VY = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 2] = it->second;

               VX = blockCoordinates[0] + iv*vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 3] = it->second;

               VY = blockCoordinates[1] + jv*vcell_length[1];
               if (fabs(VY) < EPS) VY = 0.0;
               VZ = blockCoordinates[2] + (kv + 1) * vcell_length[2];
               if (fabs(VZ) < EPS) VZ = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 4] = it->second;

               VX = blockCoordinates[0] + (iv + 1) * vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 5] = it->second;

               VY = blockCoordinates[1] + (jv + 1) * vcell_length[1];
               if (fabs(VY) < EPS) VY = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY << ' ' << VZ << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 6] = it->second;

               VX = blockCoordinates[0] + iv*vcell_length[0];
               if (fabs(VX) < EPS) VX = 0.0;
               it = nodes.find(NodeCrd<Real>(VX, VY, VZ));
               if (it == nodes.end()) cerr << "Could not find node " << VX << ' ' << VY + vcell_length[1] << ' ' << VZ + vcell_length[2] << endl;
               nodeList[b * 64 * 8 + cellInd * 8 + 7] = it->second;
            }
         }
      }
   }

   uint N_blocks = blockIds.size();
   //Block ids not needed any longer
   blockIds.clear();

   const int N_dims = 3; // Number of dimensions
   const int N_nodes = nodes.size(); // Total number of nodes
   const int N_zones = N_blocks * 64; // Total number of zones (=velocity cells)
   int shapeTypes[] = {DB_ZONETYPE_HEX}; // Hexahedrons only
   int shapeSizes[] = {8}; // Each hexahedron has 8 nodes
   int shapeCnt[] = {N_zones}; // Only 1 shape type (hexahedron)
   const int N_shapes = 1; //  -- "" --

   void* coords[3]; // Pointers to coordinate arrays
   Real * vx_crds_rotated = new Real[_node_size];
   Real * vy_crds_rotated = new Real[_node_size];
   Real * vz_crds_rotated = new Real[_node_size];
   Real * nodeCoordinates[3];
   if( !rotate ) {
      coords[0] = vx_crds;
      coords[1] = vy_crds;
      coords[2] = vz_crds;
      nodeCoordinates[0] = vx_crds;
      nodeCoordinates[1] = vy_crds;
      nodeCoordinates[2] = vz_crds;
   } else {
      //Get B vector
      array<Real, 3> B;
      if( vlsvReader.setCellIds() == false ) {
         cerr << "ERROR, FAILED TO SET CELL IDS AT " << __FILE__ << " " << __LINE__ << endl;
         delete[] vx_crds_rotated;
         delete[] vy_crds_rotated;
         delete[] vz_crds_rotated;
         return false;
      }

      //Input the B vector (or B_vol or background_b or perturbed_B depending on which of the arrays are in the vlsv file):
      vector<string> variableNames;
      variableNames.push_back("B");
      variableNames.push_back("B_vol");
      variableNames.push_back("background_B");

      bool foundB = false;

      string variableName;

      for( vector<string>::const_iterator it = variableNames.begin(); it != variableNames.end(); ++it ) {
         variableName = *it;
         if( vlsvReader.getVariable( variableName, cellID, B ) == true ) {
            // Found the B vector
            foundB = true;
            break;
         }
      }
      if( foundB == false ) {
         cerr << "ERROR, FAILED TO LOAD VARIABLE B AT " << __FILE__ << " " << __LINE__ << endl;
         delete[] vx_crds_rotated;
         delete[] vy_crds_rotated;
         delete[] vz_crds_rotated;
         return false;
      }
//      variableNames.push_back("perturbed_B");
      // If the variable we found is background_B we also need perturbed_B
      if( variableName == "background_B" ) {
         array<Real, 3> B_perturbed;
         if( vlsvReader.getVariable( "perturbed_B", cellID, B_perturbed ) == false ) { return false; }
         // Sum:
         for( uint j = 0; j < 3; ++j ) {
            B[j] = B[j] + B_perturbed[j];
         }
      }

      //Now rotate:
      //Using eigen3 library here.
      //Now we have the B vector, so now the idea is to rotate the v-coordinates so that B always faces z-direction
      //Since we're handling only one spatial cell, B is the same in every v-coordinate.

      //Rotate the v-coordinates and store them in vx_crds_rotated, vy_crds_rotated, ... :
      doRotation( vx_crds_rotated, vy_crds_rotated, vz_crds_rotated, vx_crds, vy_crds, vz_crds, B.data(), _node_size );

      coords[0] = vx_crds_rotated;
      coords[1] = vy_crds_rotated;
      coords[2] = vz_crds_rotated;
      nodeCoordinates[0] = vx_crds;
      nodeCoordinates[1] = vy_crds;
      nodeCoordinates[2] = vz_crds;
   }

   // Write zone list into silo file:
   stringstream ss;
   ss << "VelGrid";
   const string zoneListName = ss.str() + "Zones";
   //We're not writing a silo file now
   if (DBPutZonelist2(fileptr, zoneListName.c_str(), N_zones, N_dims, nodeList, nodeListSize, 0, 0, 0, shapeTypes, shapeSizes, shapeCnt, N_shapes, NULL) < 0) success = false;

   // Write grid into silo file:
   const string gridName = ss.str();
   const datatype::type siloDataType = datatype::type::FLOAT;
   const uint64_t siloDataSize = sizeof(Real);
   if (DBPutUcdmesh(fileptr, gridName.c_str(), N_dims, NULL, coords, N_nodes, N_zones, zoneListName.c_str(), NULL, SiloType(siloDataType, siloDataSize), NULL) < 0) success = false;

   delete nodeList;
   delete vx_crds;
   delete vy_crds;
   delete vz_crds;
   delete[] vx_crds_rotated;
   delete[] vy_crds_rotated;
   delete[] vz_crds_rotated;

   //Get the name of variables:
   set<string> blockVarNames;
   const string attributeName = "name";
   if (vlsvReader.getUniqueAttributeValues( "BLOCKVARIABLE", attributeName, blockVarNames) == false) {
      cerr << "ERROR, FAILED TO GET UNIQUE ATTRIBUTE VALUES AT " << __FILE__ << " " << __LINE__ << endl;
   }

   //Writing SILO file
   if (success == true) {
      for (set<string>::iterator it = blockVarNames.begin(); it != blockVarNames.end(); ++it) {
         if (convertVelocityBlockVariable( vlsvReader, meshName, gridName, N_blocks, vlsvReader.getBlockOffset(cellID), *it) == false) {
            cerr << "ERROR, convertVelocityBlockVariable FAILED AT " << __FILE__ << " " << __LINE__ << endl;
         }
      }
   }
   vlsvReader.clearCellsWithBlocks();
   //vlsvWriter.close();

   return success;*/
}

//Loads a parameter from a file
//usage: Real x = loadParameter( vlsvReader, nameOfParameter );
//Note: this is used in getCellIdFromCoords
//Input:
//[0] vlsvReader -- some VLSVReader which has a file opened
//[1] name -- name of the parameter, e.g. "xmin"
//Output:
//[0] Parameter -- Saves the parameter into the parameter variable
template <typename T>
bool loadParameter( VLSVReader& vlsvReader, const string& name, T & parameter ) {
   //Declare dataType, arraySize, vectorSize, dataSize so we know how much data we want to store
   VLSV::datatype dataType;
   uint64_t arraySize, vectorSize, dataSize; //vectorSize should be 1
   //Write into dataType, arraySize, etc with getArrayInfo -- if fails to read, give error message
   if( vlsvReader.getArrayInfo( "PARAMETERS", name, arraySize, vectorSize, dataType, dataSize ) == false ) {
      cerr << "Error, could not read parameter '" << name << "' at: " << __FILE__ << " " << __LINE__; //FIX
      MPI_Finalize();
      exit(1); //Terminate
      return false;
   }
   //Declare a buffer to write the parameter's data in (arraySize, etc was received from getArrayInfo)
   char * buffer = new char[arraySize * vectorSize * dataSize];

   //Read data into the buffer and return error if something went wrong
   if( vlsvReader.readArray( "PARAMETERS", name, 0, vectorSize, buffer ) == false ) {
      cerr << "Error, could not read parameter '" << name << "' at: " << __FILE__ << " " << __LINE__; //FIX
      MPI_Finalize();
      exit(1);
      return false;
   }
   //SHOULD be a vector of size 1 and since I am going to want to assume that, making a check here
   if( vectorSize != 1 ) {
      cerr << "Error, could not read parameter '" << name << "' at: " << __FILE__ << " " << __LINE__; //FIX
      MPI_Finalize();
      exit(1);
      return false;
   }
   //Input the parameter
   if( typeid(T) == typeid(double) ) {
      if( dataSize == 8 ) {
         parameter = *reinterpret_cast<double*>(buffer);
      } else if( dataSize == 4 ) {
         parameter = *reinterpret_cast<float*>(buffer);
      } else {
         cerr << "Error, bad datasize while reading parameters at " << __FILE__ << " " << __LINE__ << endl;
         return false;
      }
   } else if( typeid(T) == typeid(uint64_t) ) {
      if( dataSize == 8 ) {
         parameter = *reinterpret_cast<uint64_t*>(buffer);
      } else if( dataSize == 4 ) {
         parameter = *reinterpret_cast<uint32_t*>(buffer);
      } else {
         cerr << "Error, bad datasize while reading parameters at " << __FILE__ << " " << __LINE__ << endl;
         return false;
      }
   } else {
      cerr << "Error, could not read parameter '" << name << "' at: " << __FILE__ << " " << __LINE__; //FIX
      cerr << " Error message: invalid type in loadParameters" << endl;
      MPI_Finalize();
      exit(1);
      return false;
   }
   return true;
}

//Creates a cell id list of type std::unordered set and saves it in the input parameters
//Input:
//[0] vlsvReader -- some vlsv reader with a file open
//Output:
//[0] cellIdList -- Inputs a list of cell ids here
//[1] sizeOfCellIdList -- Inputs the size of the cell id list here
template <class T>
bool createCellIdList( T & vlsvReader, unordered_set<uint64_t> & cellIdList ) {
   if( cellIdList.empty() == false ) {
      cerr << "ERROR, PASSED A NON-EMPTY CELL ID LIST AT " << __FILE__ << " " << __LINE__ <<  endl;
      return false;
   }
   //meshname should be "SpatialGrid" and tag should be "CELLSWITHBLOCKS"
   const string meshName = "SpatialGrid";
   const string tagName = "CELLSWITHBLOCKS";
   //For reading in attributes 
   list< pair<string, string> > attributes;
   if( typeid(vlsvReader) == typeid(oldVlsv::Reader) ) {
      attributes.push_back( make_pair("name", meshName) );
   } else {
      attributes.push_back( make_pair("mesh", meshName) );
   }
   //Get a list of possible CellIDs from the file under CELLSWITHBLOCKS:
   //Declare vectorSize, arraySize, .., so we know the size of the array we're going to read:
   datatype::type dataType;
   uint64_t arraySize, vectorSize, dataSize; //used to store info on the data we want to retrieve (needed for readArray)
   //Read arraySize, vectorSize, dataType and dataSize and store them with getArrayInfo:
   if (vlsvReader.getArrayInfo( tagName, attributes, arraySize, vectorSize, dataType, dataSize ) == false) {
      cerr << "Could not find array " << tagName << " at: " << __FILE__ << " " << __LINE__ << endl;
      exit(1); //error, terminate program
      return false; //Shouldn't actually even get this far but whatever
   }
   //Check to make sure that the vectorSize is 1 as the CellIdList should be (Assuming so later on):
   if( vectorSize != 1 ) {
      cerr << tagName << "'s vector size is not 1 at: " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
      return false;
   }

   //We now have the arraySize and everything else needed
   //Create a buffer -- the size is determined by the data we received from getArrayInfo
   char * buffer = new char[arraySize * vectorSize * dataSize];
   const int beginningPoint = 0; //Read from the beginning ( 0 ) up to arraySize ( arraySize )
   //Read data into the buffer with readArray:
   if (vlsvReader.readArray(tagName, attributes, beginningPoint, arraySize, buffer) == false) {
      cerr << "Failed to read block metadata for mesh '" << meshName << "' at: ";
      cerr << __FILE__ << " " << __LINE__ << endl;
      delete buffer;
      exit(1);
      return false;
   }


   //Reinterpret the buffer and point cellIdList in the right direction:
   uint64_t * _cellIdList = reinterpret_cast<uint64_t*>(buffer);
   //Reserve space for the cell id list:
   cellIdList.rehash( (uint64_t)(arraySize * 1.25) );
   for( uint64_t i = 0; i < arraySize; ++i ) {
      cellIdList.insert( (uint64_t)( _cellIdList[i] ) );
   }
   delete[] buffer;
   return true;
}



//Calculates the cell coordinates and outputs into *coordinates 
//NOTE: ASSUMING COORDINATES IS NOT NULL AND IS OF SIZE 3
//Input:
//[0] CellStructure cellStruct -- A struct for holding cell information. Has the cell length in x,y,z direction, for example
//[1] uint64_t cellId -- Some given cell id
//Output:
//[0] Real * coordinates -- Some coordinates x, y, z (NOTE: the vector size should be 3!)
void getCellCoordinates( const CellStructure & cellStruct, const uint64_t cellId, Real * coordinates ) {
   //Check for null pointer
   if( !coordinates ) {
      cerr << "Passed invalid pointer at: " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   //Calculate the cell coordinates in block coordinates (so in the cell grid where the coordinates are integers)
   uint64_t currentCellCoordinate[3];
   //Note: cell_bounds is a variable that tells the length of a cell in x, y or z direction (depending on the index)
   //cellStruct is a struct that holds info on the cell structure used in simulation (such as the length of the cell and the mininum
   //value of x within the cell grid)
   currentCellCoordinate[0] = cellId % cellStruct.cell_bounds[0];
   currentCellCoordinate[1] = ((cellId - currentCellCoordinate[0]) / cellStruct.cell_bounds[0]) % cellStruct.cell_bounds[1];
   currentCellCoordinate[2] = ((cellId - cellStruct.cell_bounds[0]*currentCellCoordinate[1]) / (cellStruct.cell_bounds[0]*cellStruct.cell_bounds[1]));
   //the currentCellCoordinate is always off by one -- This is just a matter of how stuff has been calculated. If cell bounds and
   //other stuff were defined slightly in other parts of this code differently, this would not be needed.
   currentCellCoordinate[0] -= 1;
   //Get the coordinates of the cell. These are the coordinates in actual space (not cell coordinates, which are integers from 1 up to some number)
   coordinates[0] = cellStruct.min_coordinates[0] + currentCellCoordinate[0] * cellStruct.cell_length[0];
   coordinates[1] = cellStruct.min_coordinates[1] + currentCellCoordinate[1] * cellStruct.cell_length[1];
   coordinates[2] = cellStruct.min_coordinates[2] + currentCellCoordinate[2] * cellStruct.cell_length[2];
   //all done
   return;
}

//Searches for the closest cell id to the given coordinates from a list of cell ids and returns it
//Input:
//[0] CellStructure cellStruct -- a struct that holds info on cell structure
//[1] uint64_t * cellIdList -- Some list of cell ids (Note: Could use a vector here)
//[2] Real * coordinates, -- Some coordinates x, y, z  (Note: Could use std::array here)
//[3] uint64_t sizeOfCellIdList -- Size of cellIdList (Note: This would not be needed if a vector was used)a
//Output:
//[0] Returns the closest cell id to the given coordinates
uint64_t searchForBestCellId( const CellStructure & cellStruct,
                              const uint64_t * cellIdList, 
                              const Real * coordinates, 
                              const uint64_t sizeOfCellIdList ) {
   //Check for null pointer:
   if( !cellIdList || !coordinates ) {
      cerr << "Error at: ";
      cerr << __FILE__ << " " << __LINE__;
      cerr << ", passed a null pointer to searchForBestCellId" << endl;
      exit(1);
      return 0;
   }
   //Create variables to help iterate through cellIdList. (Used to keep track of the best cell id and best distance so far)
   Real bestDistance = numeric_limits<Real>::max();
   Real bestCellId = numeric_limits<uint64_t>::max();
   //Iterate through the list of cell id candidates ( cell ids with distribution )
   for( uint64_t i = 0; i < sizeOfCellIdList; ++i ) {
      //Get coordinates from the cell currently being handled in the iteration:
      const uint64_t currentCell = cellIdList[i];
      //Create cellCoordinate and store the current cell id's coordinates in there
      const size_t _size = 3;
      Real cellCoordinate[_size];
      //Stores the current cell's coordinates into cellCoordinate
      getCellCoordinates( cellStruct, currentCell, cellCoordinate );
      //Calculate distance from cell coordinates to input coordinates
      Real dist = ( 
                   (cellCoordinate[0] - coordinates[0]) * (cellCoordinate[0] - coordinates[0])
                   + (cellCoordinate[1] - coordinates[1]) * (cellCoordinate[1] - coordinates[1])
                   + (cellCoordinate[2] - coordinates[2]) * (cellCoordinate[2] - coordinates[2])
                  );
      //if the distance from the given coordinates to the cell coordinates is the best so far, set that cell id as the best cell id
      if( bestDistance > dist ) {
         bestDistance = dist;
         bestCellId = currentCell;
      }
   }
   //return the best cell id:
   return bestCellId;
}

//Searches for the closest cell id to the given coordinates from a list of cell ids and returns it
//Input:
//[0] CellStructure cellStruct -- a struct that holds info on cell structure
//[1] cellIdList -- Some list of cell ids
//[2] coordinates, -- Some coordinates x, y, z 
//Output:
//[0] Returns the closest cell id to the given coordinates
uint64_t searchForBestCellId( const CellStructure & cellStruct,
                              const unordered_set<uint64_t> & cellIdList, 
                              const array<Real, 3> coordinates ) {
   //Check for null pointer:
   if( coordinates.empty() ) {
      cerr << "ERROR, PASSED AN EMPTY COORDINATES AT " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   if( cellIdList.empty() ) {
      cerr << "ERROR, PASSED AN EMPTY CELL ID LIST AT " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }

   //Get the cell id corresponding to the given coordinates:
   int cellCoordinates[3];
   for( unsigned int i = 0; i < 3; ++i ) {
      //Note: Cell coordinates work like this:
      //cell id = z * (num. of cell in y-direction) * (num. of cell in z-direction) + y * (num. of cell in x-direction) + x
      cellCoordinates[i] = floor((coordinates[i] - cellStruct.min_coordinates[i]) / cellStruct.cell_length[i]);
      if( cellCoordinates[i] < 0 ) {
         cerr << "Coordinates out of bounds at " << __FILE__ << " " << __LINE__ << endl;
         return numeric_limits<uint64_t>::max();
      }
   }

   //Return the cell id at cellCoordinates:
   //Note: In vlasiator, the cell ids start from 1 hence the '+ 1'
   
   return ( (uint64_t)(
            cellCoordinates[2] * cellStruct.cell_bounds[1] * cellStruct.cell_bounds[0]
            + cellCoordinates[1] * cellStruct.cell_bounds[0]
            + cellCoordinates[0] + 1
          ) );
}



//Initalizes cellStruct
//Input:
//[0] oldVlsv::Reader vlsvReader -- some reader with a file open (used for loading parameters)
//Output:
//[0] CellStructure cellStruct -- Holds info on cellStruct. The members are given the correct values here (Note: CellStructure could be made into a class
//instead of a struct with this as the constructor but since a geometry class has already been coded before, it would be a waste)
void setCellVariables( oldVlsv::Reader & vlsvReader, CellStructure & cellStruct ) {
   //Get x_min, x_max, y_min, y_max, etc so that we know where the given cell id is in (loadParameter returns char*, hence the cast)
   Real x_min, x_max, y_min, y_max, z_min, z_max;
   loadParameter( vlsvReader, "xmin", x_min );
   loadParameter( vlsvReader, "xmax", x_max );
   loadParameter( vlsvReader, "ymin", y_min );
   loadParameter( vlsvReader, "ymax", y_max );
   loadParameter( vlsvReader, "zmin", z_min );
   loadParameter( vlsvReader, "zmax", z_max );

   //Number of cells in x, y, z directions (used later for calculating where in the cell coordinates (which are ints) the given
   //coordinates are) (Done in 
   //There's x, y and z coordinates so the number of different coordinates is 3:
   const int NumberOfCoordinates = 3;
   uint64_t cell_bounds[NumberOfCoordinates];
   //Get the number of cells in x,y,z direction from the file:
   //x-direction
   loadParameter( vlsvReader, "xcells_ini", cell_bounds[0] );
   //y-direction
   loadParameter( vlsvReader, "ycells_ini", cell_bounds[1] );
   //z-direction
   loadParameter( vlsvReader, "zcells_ini", cell_bounds[2] );

   //Now we have the needed variables, so let's calculate how much in one block equals in length:
   //Total length of x, y, z:
   Real x_length = x_max - x_min;
   Real y_length = y_max - y_min;
   Real z_length = z_max - z_min;
   //Set the cell structure properly:
   for( int i = 0; i < NumberOfCoordinates; ++i ) {
      cellStruct.cell_bounds[i] = cell_bounds[i];
   }
   //Calculate the cell length
   cellStruct.cell_length[0] = ( x_length / (Real)(cell_bounds[0]) );
   cellStruct.cell_length[1] = ( y_length / (Real)(cell_bounds[1]) );
   cellStruct.cell_length[2] = ( z_length / (Real)(cell_bounds[2]) );
   //Calculate the minimum coordinates
   cellStruct.min_coordinates[0] = x_min;
   cellStruct.min_coordinates[1] = y_min;
   cellStruct.min_coordinates[2] = z_min;

   return;
}

//Initalizes cellStruct
//Input:
//[0] vlsv::Reader vlsvReader -- some reader with a file open (used for loading parameters)
//Output:
//[0] CellStructure cellStruct -- Holds info on cellStruct. The members are given the correct values here (Note: CellStructure could be made into a class
//instead of a struct with this as the constructor but since a geometry class has already been coded before, it would be a waste)
void setCellVariables( Reader & vlsvReader, CellStructure & cellStruct ) {
   //Get x_min, x_max, y_min, y_max, etc so that we know where the given cell id is in (loadParameter returns char*, hence the cast)
   //Note: Not actually sure if these are Real valued or not
   Real x_min, x_max, y_min, y_max, z_min, z_max, vx_min, vx_max, vy_min, vy_max, vz_min, vz_max;
   //Read in the parameter:
   if( vlsvReader.readParameter( "xmin", x_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "xmax", x_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "ymin", y_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "ymax", y_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "zmin", z_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "zmax", z_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;

   if( vlsvReader.readParameter( "vxmin", vx_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "vxmax", vx_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "vymin", vy_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "vymax", vy_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "vzmin", vz_min ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   if( vlsvReader.readParameter( "vzmax", vz_max ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;

   //Number of cells in x, y, z directions (used later for calculating where in the cell coordinates the given
   //coordinates are) (Done in getCellCoordinates)
   //There's x, y and z coordinates so the number of different coordinates is 3:
   const short int NumberOfCoordinates = 3;
   uint64_t cell_bounds[NumberOfCoordinates];
   uint64_t vcell_bounds[NumberOfCoordinates];
   //Get the number of velocity blocks in x,y,z direction from the file:
   //x-direction
   if( vlsvReader.readParameter( "vxblocks_ini", vcell_bounds[0] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   //y-direction
   if( vlsvReader.readParameter( "vyblocks_ini", vcell_bounds[1] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   //z-direction
   if( vlsvReader.readParameter( "vzblocks_ini", vcell_bounds[2] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   //Get the number of spatial cells in x,y,z direction from the file:
   //x-direction
   if( vlsvReader.readParameter( "xcells_ini", cell_bounds[0] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   //y-direction
   if( vlsvReader.readParameter( "ycells_ini", cell_bounds[1] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;
   //z-direction
   if( vlsvReader.readParameter( "zcells_ini", cell_bounds[2] ) == false ) cerr << "FAILED TO READ PARAMETER AT " << __FILE__ << " " << __LINE__ << endl;

   //Now we have the needed variables, so let's calculate how much in one block equals in length:
   //Total length of x, y, z:
   Real x_length = x_max - x_min;
   Real y_length = y_max - y_min;
   Real z_length = z_max - z_min;

   Real vx_length = vx_max - vx_min;
   Real vy_length = vy_max - vy_min;
   Real vz_length = vz_max - vz_min;
   //Set the cell structure properly:
   for( int i = 0; i < NumberOfCoordinates; ++i ) {
      cellStruct.cell_bounds[i] = cell_bounds[i];
      cellStruct.vcell_bounds[i] = vcell_bounds[i];
   }
   //Calculate the cell length
   cellStruct.cell_length[0] = ( x_length / (Real)(cell_bounds[0]) );
   cellStruct.cell_length[1] = ( y_length / (Real)(cell_bounds[1]) );
   cellStruct.cell_length[2] = ( z_length / (Real)(cell_bounds[2]) );

   //Calculate the velocity cell length
   cellStruct.vblock_length[0] = ( vx_length / (Real)(vcell_bounds[0]) );
   cellStruct.vblock_length[1] = ( vy_length / (Real)(vcell_bounds[1]) );
   cellStruct.vblock_length[2] = ( vz_length / (Real)(vcell_bounds[2]) );
   //Calculate the minimum coordinates
   cellStruct.min_coordinates[0] = x_min;
   cellStruct.min_coordinates[1] = y_min;
   cellStruct.min_coordinates[2] = z_min;
   //Calculate the minimum coordinates for velocity cells
   cellStruct.min_vcoordinates[0] = vx_min;
   cellStruct.min_vcoordinates[1] = vy_min;
   cellStruct.min_vcoordinates[2] = vz_min;


   for( int i = 0; i < 3; ++i ) {
      if( cellStruct.cell_length[i] == 0 || cellStruct.cell_bounds[i] == 0 || cellStruct.vblock_length[i] == 0 || cellStruct.vcell_bounds[i] == 0 ) {
         cerr << "ERROR, ZERO CELL LENGTH OR CELL_BOUNDS AT " << __FILE__ << " " << __LINE__ << endl;
         exit(1);
      }
   }
   
   // By default set an unrefined velocity mesh. Then check if the max refinement level 
   // was actually given as a parameter.
   uint32_t dummyUInt;
   cellStruct.maxVelRefLevel = 0;
   if (vlsvReader.readParameter("max_velocity_ref_level",dummyUInt) == true) {
      cellStruct.maxVelRefLevel = dummyUInt;
   }
   
   return;
}




//Returns a cell id based on some given coordinates
//Returns numeric_limits<uint64_t>::max(), if the distance from the coordinates to cell id is larger than max_distance
//Input:
//[0] vlsv::Reader& vlsvReader -- Some vlsvReader (with a file open)
//[1] Real * coords -- Some given coordinates (in this file the coordinates are retrieved from the user as an input)
//Note: Assuming coords is a pointer of size 3
//[2] max_distance -- Max allowed distance between the given coordinates *coords and the returned cell id's coordinates
//Output:
//[0] Returns the cell id in uint64_t
uint64_t getCellIdFromCoords( const CellStructure & cellStruct, 
                              const unordered_set<uint64_t> cellIdList,
                              const array<Real, 3> coords) {
   if( coords.empty() ) {
      cerr << "ERROR, PASSED AN EMPTY STD::ARRAY FOR COORDINATES AT " << __FILE__ << " " << __LINE__ << endl;
   }


   //Check for empty vectors
   if( cellIdList.empty() ) {
      cerr << "Invalid cellIdList at " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }
   if( coords.empty() ) {
      cerr << "Invalid coords at " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   }


   //Now pick the closest cell id to the given coordinates:
   uint64_t cellId = searchForBestCellId( cellStruct, cellIdList, coords );

   //Check to make sure the cell id has distribution (It does if it's in the list of cell ids)
   unordered_set<uint64_t>::const_iterator foundCellId = cellIdList.find( cellId );
   if( foundCellId == cellIdList.end() ) {
      //Didn't find the cell id from the list of possible cell ids so return numerical limit:
      return numeric_limits<uint64_t>::max();
   }

   //Everything ok, return the cell id:
   return cellId;
}




//Prints out the usage message
void printUsageMessage() {
   cout << endl;
   cout << "USAGE: ./vlsvextract <file name mask> <options>" << endl;
   cout << endl;
   cout << "To get a list of options use --help" << endl;
   cout << endl;
}


//Used in main() to retrieve options (returns false if something goes wrong)
//Input:
//[0] int argn -- number of arguments in args
//[1] char *args -- arguments
//Output:
//[0] UserOptions & mainOptions -- Saves all the options in this class
bool retrieveOptions( const int argn, char *args[], UserOptions & mainOptions ) {
   //Get variables from mainOptions
   bool & getCellIdFromCoordinates = mainOptions.getCellIdFromCoordinates;
   bool & getCellIdFromInput = mainOptions.getCellIdFromInput;
   bool & getCellIdFromLine = mainOptions.getCellIdFromLine;
   bool & rotateVectors = mainOptions.rotateVectors;
   bool & plasmaFrame = mainOptions.plasmaFrame;
   uint64_t & cellId = mainOptions.cellId;
   uint32_t & numberOfCoordinatesInALine = mainOptions.numberOfCoordinatesInALine;
   vector<string>  & outputDirectoryPath = mainOptions.outputDirectoryPath;
   array<Real, 3> & coordinates = mainOptions.coordinates;
   array<Real, 3> & point1 = mainOptions.point1;
   array<Real, 3> & point2 = mainOptions.point2;

   //By default every bool input should be false and vectors should be empty
   if( getCellIdFromCoordinates == true || rotateVectors == true || plasmaFrame == true || getCellIdFromInput == true || getCellIdFromLine == true || outputDirectoryPath.empty() == false ) {
      cerr << "Error at: " << __FILE__ << " " << __LINE__ << ", invalid arguments in retrieveOptions()" << endl;
      return false;
   }
   try {
      //Create an options_description
      po::options_description desc("Options");
      //Add options -- cellID takes input of type uint64_t and coordinates takes a Real-valued std::vector
      desc.add_options()
         ("help", "display help")
         ("cellid", po::value<uint64_t>(), "Set cell id")
         ("rotate", "Rotate velocities so that they face z-axis")
         ("plasmaFrame", "Shift the distribution so that the bulk velocity is 0")
         ("coordinates", po::value< vector<Real> >()->multitoken(), "Set spatial coordinates x y z")
         ("unit", po::value<string>(), "Sets the units. Options: re, km, m (OPTIONAL)")
         ("point1", po::value< vector<Real> >()->multitoken(), "Set the starting point x y z of a line")
         ("point2", po::value< vector<Real> >()->multitoken(), "Set the ending point x y z of a line")
         ("pointamount", po::value<unsigned int>(), "Number of points along a line (OPTIONAL)")
         ("outputdirectory", po::value< vector<string> >(), "The directory where the file is saved (default current folder) (OPTIONAL)");
         
      //For mapping input
      po::variables_map vm;
      //Store input into vm (Don't allow short options)
      po::store(po::parse_command_line(argn, args, desc, po::command_line_style::unix_style ^ po::command_line_style::allow_short), vm);
      po::notify(vm);
      //Check if help was prompted
      if( vm.count("help") ) {
         //Display options
         cout << desc << endl;
         return false;
      }
      //Check if coordinates have been input and make sure there's only 3 coordinates
      const size_t _size = 3;
      if( !vm["coordinates"].empty() && vm["coordinates"].as< vector<Real> >().size() == _size ) {
        //Save input into coordinates vector (later on the values are stored into a *Real pointer
        vector<Real> _coordinates = vm["coordinates"].as< vector<Real> >();
        for( uint i = 0; i < 3; ++i ) {
           coordinates[i] = _coordinates[i];
        }
        //Let the program know we want to get the cell id from coordinates
        getCellIdFromCoordinates = true;
      }
      if( !vm["point1"].empty() && vm["point1"].as< vector<Real> >().size() == _size
       && !vm["point2"].empty() && vm["point2"].as< vector<Real> >().size() == _size ) {
        //Save input into point vector (later on the values are stored into a *Real pointer
        vector<Real> _point1 = vm["point1"].as< vector<Real> >();
        vector<Real> _point2 = vm["point2"].as< vector<Real> >();
        //Input the values
        for( uint i = 0; i < 3; ++i ) {
           point1[i] = _point1[i];
           point2[i] = _point2[i];
        }
        _point1.clear();
        _point2.clear();
        //Check if the user wants to specify number of coordinates we want to calculate:
        if( vm.count("pointAmount") ) {
           //User specified the number of points -- set it
           numberOfCoordinatesInALine = vm["pointAmount"].as<uint32_t>();
        }
        //Let the program know we want to get the cell id from coordinates
        getCellIdFromLine = true;
      }
      //Check for rotation
      if( vm.count("rotate") ) {
         //Rotate the vectors (used in convertVelocityBlocks2 as an argument)
         rotateVectors = true;
      }
      //Check for plasma frame shifting
      if( vm.count("plasmaFrame") ) {
         // Shift the velocity distribution to plasma frame
         plasmaFrame = true;
      }
      //Check for cell id input
      if( vm.count("cellid") ) {
         //Save input
         cellId = vm["cellid"].as<uint64_t>();
         getCellIdFromInput = true;
      }
      if( vm.count("outputdirectory") ) {
         //Save input
         outputDirectoryPath = vm["outputdirectory"].as< vector<string> >();
         //Make sure the vector is of length 1:
         if( outputDirectoryPath.size() != 1 ) {
            return false;
         }
         //If '/' or '\' was not added to the end of the path, add it:
         string & pathName = outputDirectoryPath.back();
         //Find the last index of a char with '\' or '/'
         const unsigned index = pathName.find_last_of("/\\");
         //Check if the last index is '/' or '\':
         if( index != (pathName.length() - 1) ) {
            //Make sure both '/' and '\' were not used:
            const size_t index1 = pathName.find("/");
            const size_t index2 = pathName.find("\\");
            //Check if the character was found:
            if( index1 != string::npos && index2 != string::npos ) {
               cout << "Do not use both '/' and '\\' in directory path! " << index1 << " " << index2 << endl;
               cout << desc << endl;
               return false;
            } else if( index1 != string::npos ) {
               //The user used '/' in the path
               const char c = '/';
               //Add '/' at the end
               pathName.append( 1, c );
            } else {
               //The user used '/' in the path
               const char c = '\\';
               //Add '\' at the end
               pathName.append( 1, c );
            }
         }
      } else {
         string defaultPath = "";
         outputDirectoryPath.push_back(defaultPath);
      }
      //Declare unit conversion variable (the variable which will multiply coordinates -- by default 1)
      Real unit_conversion = 1;
      if( vm.count("unit") ) {
         //Get the input into 'unit'
         const string unit = vm["unit"].as<string>();
         if( unit.compare( "re" ) == 0 ) {
            //earth radius
            unit_conversion = 6371000;
         } else if( unit.compare( "km" ) == 0 ) {
            //km
            unit_conversion = 1000;
         } else if( unit.compare( "m" ) == 0 ) {
            //meters
            unit_conversion = 1;
         } else {
            //No known unit
            cout << "Invalid unit!" << endl;
            cout << desc << endl;
            return false;
         }
         //Convert the coordinates into correct units:
//getCellIdFromLine, getCellIdFromCoordinates,
         if( getCellIdFromLine ) {
            const uint16_t vectorSize = 3;
            for( uint i = 0; i < vectorSize; ++i ) {
               //Multiply the coordinates:
               point1[i] = point1[i] * unit_conversion;
               point2[i] = point2[i] * unit_conversion;
            }
         } else if( getCellIdFromCoordinates ) {
            const uint16_t vectorSize = 3;
            for( uint i = 0; i < vectorSize; ++i ) {
               //Multiply the coordinates:
               coordinates[i] = coordinates[i] * unit_conversion;
            }
         } else {
            cout << "Nothing to convert!" << endl;
            cout << desc << endl;
            return false;
         }
      }

      //Make sure the input is correct:
      //The cell id can be either received from input or calculated from coordinates or from a line, but only one option is ok:
      //Also, we have to get the cell id from somewhere so either cell id must be input or coordinates/line must be input
      int count = 0;
      if( getCellIdFromLine ) ++count;
      if( getCellIdFromInput ) ++count;
      if( getCellIdFromCoordinates ) ++count;
      if( count != 1 ) {
         //Wrong number of arguments
         cout << "Contradiction in the way of retrieving cell id ( can only be 1 out of 3 options )" << endl;
         return false;
      }
   } catch( exception &e ) {
      cerr << "Error " << e.what() << " at " << __FILE__ << " " << __LINE__ << endl;
      return false;
   } catch( ... ) {
      cerr << "Unknown error" << " at " << __FILE__ << " " << __LINE__ << endl;
      return false;
   }
   //Check to make sure the input for outputDirectoryPath is valid
   if( outputDirectoryPath.size() != 1 ) {
      cerr << "Error at: " << __FILE__ << " " << __LINE__ << ", invalid outputDirectoryPath!" << endl;
      exit(1);
   }
   //Everything ok
   return true;
}

//Outputs a number of coordinates along a line whose starting point is start and ending point end into outPutCoordinates
//Input:
//[0] array<Real, 3> & start -- Starting x, y, z coordinates of a line
//[1] array<Real, 3> & end -- Starting x, y, z coordinates of a line
//[2] unsigned int numberOfCoordinates -- Number of coordinates stored into outputCoordinates
//Output:
//[0] vector< array<Real, 3> > & outputCoordinates -- Stores the coordinates here
//Example: setCoordinatesAlongALine( {0,0,0}, {3,0,0}, 4, output ) would store coordinates {0,0,0}, {1,0,0}, {2,0,0}, {3,0,0} in
//output
void setCoordinatesAlongALine( 
                               const CellStructure & cellStruct,
                               const array<Real, 3> & start, const array<Real, 3> & end, uint32_t numberOfCoordinates,
                               vector< array<Real, 3> > & outputCoordinates 
                             ) {
   //Used in calculations in place of numberOfCoordinates
   uint32_t _numberOfCoordinates;
   //make sure the input is valid
   if( numberOfCoordinates == 0 ) {
      //Default value -- determine the number of coordinates yourself (Should be about the same size as the number of cells along
      //the line
      //Calculate the length of the line:
      const Real line_length = sqrt(
                                     (end[0] - start[0]) * (end[0] - start[0])
                                   + (end[1] - start[1]) * (end[1] - start[1])
                                   + (end[2] - start[2]) * (end[2] - start[2])
                                   );
      Real minCellLength = numeric_limits<Real>::max();

      const uint32_t sizeOfCellLength = 3;
      //Get the smallest cell length (usually they're all the same size)
      for( uint i = 0; i < sizeOfCellLength; ++i ) {
         if( minCellLength > cellStruct.cell_length[i] ) {minCellLength = cellStruct.cell_length[i];}
      }

      if( minCellLength == 0 ) {
         cerr << "ERROR, BAD MINIMUM CELL LENGTH AT " << __FILE__ << " " << __LINE__ << endl;
         exit(1);
      }
      _numberOfCoordinates = (uint32_t)( line_length / minCellLength );

      //Make sure the number is valid (Must be at least 2 points):
      if( _numberOfCoordinates < 2 ) {
         cerr << "Cannot use numberOfCoordinates lower than 2 at " << __FILE__ << " " << __LINE__ << endl;
         exit(1);
      }

      //Just to make sure that there's enough coordinates let's add a few more:
      _numberOfCoordinates = (uint32_t)(1.2 * _numberOfCoordinates);
   } else if( numberOfCoordinates < 2 ) {
      cerr << "Cannot use numberOfCoordinates lower than 2 at " << __FILE__ << " " << __LINE__ << endl;
      exit(1);
   } else {
      //User defined input
      _numberOfCoordinates = numberOfCoordinates;
   }
   //Store the unit of line vector ( the vector from start to end divided by the numberOfCoordinates ) into line_unit
   array<Real, 3> line_unit;
   for( uint i = 0; i < 3; ++i ) {
      line_unit[i] = (end[i] - start[i]) / (Real)(_numberOfCoordinates - 1);
   }

   //Insert the coordinates:
   outputCoordinates.reserve(_numberOfCoordinates);
   for( uint j = 0; j < _numberOfCoordinates; ++j ) {
      const array<Real, 3> input{{start[0] + j * line_unit[0],
                                  start[1] + j * line_unit[1],
                                  start[2] + j * line_unit[2],}};
      outputCoordinates.push_back(input);
   }

   //Make sure the output is not empty
   if( outputCoordinates.empty() ) {
      cerr << "Error at: " << __FILE__ << " " << __LINE__ << ", Calculated coordinates empty!" << endl;
      exit(1);
   }
   return;
}


template <class T>
void convertFileToSilo( const string & fileName, const UserOptions & mainOptions ) {
   T vlsvReader;
   // Open VLSV file and read mesh names:
   vlsvReader.open(fileName);
   list<string> meshNames;
   const string tagName = "MESH";
   const string attributeName = "name";
   
   if (vlsvReader.getMeshNames(meshNames) == false) {
      cout << "\t file '" << fileName << "' not compatible" << endl;
      vlsvReader.close();
      return;
   }

   //Sets cell variables (for cell geometry) -- used in getCellIdFromCoords function
   CellStructure cellStruct;
   setCellVariables( vlsvReader, cellStruct );

   //Declare a vector for holding multiple cell ids (Note: Used only if we want to calculate the cell id along a line)
   vector<uint64_t> cellIdList;

   //Determine how to get the cell id:
   //(getCellIdFromCoords might as well take a vector parameter but since I have not seen many vectors used, I'm keeping to
   //previously used syntax)
   if( mainOptions.getCellIdFromCoordinates ) {

      //Get the cell id list of cell ids with velocity distribution
      unordered_set<uint64_t> cellIdList_velocity;
      createCellIdList( vlsvReader, cellIdList_velocity );

      //Get the cell id from coordinates
      //Note: By the way, this is not the same as bool getCellIdFromCoordinates (should change the name)
      const uint64_t cellID = getCellIdFromCoords( cellStruct, cellIdList_velocity, mainOptions.coordinates );

      if( cellID == numeric_limits<uint64_t>::max() ) {
         //Could not find a cell id
         cout << "Could not find a cell id in the given coordinates!" << endl;
         vlsvReader.close();
         return;
      }

      //Print the cell id:
      //store the cel lid in the list of cell ids (This is only used because it makes the code for 
      //calculating the cell ids from a line clearer)
      cellIdList.push_back( cellID );
   } else if( mainOptions.getCellIdFromLine ) {
      //Get the cell id list of cell ids with velocity distribution
      unordered_set<uint64_t> cellIdList_velocity;
      createCellIdList( vlsvReader, cellIdList_velocity );

      //Now there are multiple cell ids so do the same treatment for the cell ids as with getCellIdFromCoordinates
      //but now for multiple cell ids

      //Declare a vector for storing coordinates:
      vector< array<Real, 3> > coordinateList;
      //Store cell ids into coordinateList:
      //Note: All mainOptions are user-input
      setCoordinatesAlongALine( cellStruct, mainOptions.point1, mainOptions.point2, mainOptions.numberOfCoordinatesInALine, coordinateList );
      //Note: (getCellIdFromCoords might as well take a vector parameter but since I have not seen many vectors used,
      // I'm keeping to previously used syntax)
      //Declare an iterator
      vector< array<Real, 3> >::iterator it;
      //Calculate every cell id in coordinateList
      for( it = coordinateList.begin(); it != coordinateList.end(); ++it ) {
         //NOTE: since this code is nearly identical to the code for calculating single coordinates, it could be smart to create a separate function for this
         //declare coordinates array
         const array<Real, 3> & coords = *it;
         //Get the cell id from coordinates
         const uint64_t cellID = getCellIdFromCoords( cellStruct, cellIdList_velocity, coords );
         if( cellID != numeric_limits<uint64_t>::max() ) {
            //A valid cell id:
            //Store the cell id in the list of cell ids but only if it is not already there:
            if( cellIdList.empty() ) {
               //cell id list empty so it's safe to input
               cellIdList.push_back( cellID );
            } else if( cellIdList.back() != cellID ) {
               //cellID has not already been added, so add it now:
               cellIdList.push_back( cellID );
            }
         }
      }
   } else if( mainOptions.getCellIdFromInput ) {
      //Declare cellID and set it if the cell id is specified by the user
      //bool getCellIdFromLine equals true) -- this is done later on in the code ( After the file has been opened)
      const uint64_t cellID = mainOptions.cellId;
      //store the cell id in the list of cell ids (This is only used because it makes the code for 
      //calculating the cell ids from a line clearer)
      cellIdList.push_back( cellID );
   } else {
      //This should never happen but it's better to be safe than sorry
      cerr << "Error at: " << __FILE__ << " " << __LINE__ << ", No user input for cell id retrieval!" << endl;
      vlsvReader.close();
      exit(1);
   }

   //Check for proper input
   if( cellIdList.empty() ) {
      cout << "Could not find a cell id!" << endl;
      return;
   }

   //Next task is to iterate through the cell ids and save files:
   //Save all of the cell ids' velocities into files:
   vector<uint64_t>::iterator it;
   //declare extractNum for keeping track of which extraction is going on and informing the user (used in the iteration)
   int extractNum = 1;
   //Give some info on how many extractions there are and what the save path is:
   cout << "Save path: " << mainOptions.outputDirectoryPath.front() << endl;
   cout << "Total number of extractions: " << cellIdList.size() << endl;
   //Iterate:
   for( it = cellIdList.begin(); it != cellIdList.end(); ++it ) {
      //get the cell id from the iterator:
      const uint64_t cellID = *it;
      //Print out the cell id:
      cout << "Cell id: " << cellID << endl;
      // Create a new file suffix for the output file:
      stringstream ss1;
      //ss1 << ".silo";
      ss1 << ".vlsv";
      string newSuffix;
      ss1 >> newSuffix;

      // Create a new file prefix for the output file:
      stringstream ss2;
      ss2 << "velgrid" << '.';
      if( mainOptions.rotateVectors ) {
         ss2 << "rotated" << '.';
      }
      if( mainOptions.plasmaFrame ) {
         ss2 << "shifted" << '.';
      }
      ss2 << cellID;
      string newPrefix;
      ss2 >> newPrefix;
      
      // Replace .vlsv with the new suffix:
      string outputFileName = fileName;
      size_t pos = outputFileName.rfind(".vlsv");
      if (pos != string::npos) outputFileName.replace(pos, 5, newSuffix);
   
      pos = outputFileName.find(".");
      if (pos != string::npos) outputFileName.replace(0, pos, newPrefix);

      string slicePrefix = "VelSlice";
      string outputSliceName = fileName;
      pos = outputSliceName.find(".");
      if (pos != string::npos) outputSliceName.replace(0,pos,slicePrefix);

      //Declare the file path (used in DBCreate to save the file in the correct location)
      string outputFilePath;
      //Get the path (outputDirectoryPath was retrieved from user input and it's a vector<string>):
      outputFilePath.append( mainOptions.outputDirectoryPath.front() );
      //The complete file path is still missing the file name, so add it to the end:
      outputFilePath.append( outputFileName );      

      // Create a SILO file for writing:
      fileptr = DBCreate(outputFilePath.c_str(), DB_CLOBBER, DB_LOCAL, "Vlasov data file", DB_PDB);
      if (fileptr == NULL) {
         cerr << "\t failed to create output SILO file for input file '" << fileName << "'" << endl;
         DBClose(fileptr);
         vlsvReader.close();
         return;
      }

      // Extract velocity grid from VLSV file, if possible, and convert into SILO format:
      bool velGridExtracted = true;
      if( typeid(vlsvReader) == typeid(newVlsv::Reader) ) {
         vlsvReader.setCellsWithBlocks();
      }
      for (list<string>::const_iterator it2 = meshNames.begin(); it2 != meshNames.end(); ++it2) {
         convertSlicedVelocityMesh(vlsvReader,outputSliceName,*it2,cellStruct);
         if (convertVelocityBlocks2(vlsvReader, outputFilePath, *it2, cellStruct, cellID, mainOptions.rotateVectors, mainOptions.plasmaFrame ) == false) {
            velGridExtracted = false;
         } else {
            //Display message for the user:
            if( mainOptions.getCellIdFromLine ) {
               //Extracting multiple cell ids:
               //Display how mant extracted and how many more to go:
               int moreToGo = cellIdList.size() - extractNum;
               //Display message
               cout << "Extracted num. " << extractNum << ", " << moreToGo << " more to go" << endl;
               //Move to the next extraction number
               ++extractNum;
            } else {
               //Single cell id:
               cout << "\t extracted from '" << fileName << "'" << endl;
            }
         }
      }
      DBClose(fileptr);

      // If velocity grid was not extracted, delete the SILO file:
      if (velGridExtracted == false) {
         cerr << "ERROR, FAILED TO EXTRACT VELOCITY GRID AT: " << __FILE__ << " " << __LINE__ << endl;
         if (remove(outputFilePath.c_str()) != 0) {
            cerr << "\t ERROR: failed to remote dummy output file!" << endl;
         }
      }
   }

   vlsvReader.close();

}

int main(int argn, char* args[]) {
   int ntasks, rank;
   MPI_Init(&argn, &args);
   MPI_Comm_size(MPI_COMM_WORLD, &ntasks);
   MPI_Comm_rank(MPI_COMM_WORLD, &rank);

   //Get the file name
   const string mask = args[1];  

   const string directory = ".";
   const string suffix = ".vlsv";
   DIR* dir = opendir(directory.c_str());
   if (dir == NULL) {
      cerr << "ERROR in reading directory contents!" << endl;
      closedir(dir);
      return 1;
   }

   int filesFound = 0, entryCounter = 0;
   vector<string> fileList;
   struct dirent* entry = readdir(dir);
   while (entry != NULL) {
      const string entryName = entry->d_name;
      if (entryName.find(mask) == string::npos || entryName.find(suffix) == string::npos) {
         entry = readdir(dir);
         continue;
      }
      fileList.push_back(entryName);
      filesFound++;
      entry = readdir(dir);
   }
   if (filesFound == 0) cout << "\t no file found, check name and read and write rights, vlsvextract currently supports only extracting from the folder where the vlsv file resides (you can get past this by linking the vlsv file" << endl;
   closedir(dir);

   //Retrieve options variables:
   UserOptions mainOptions;

   //Get user input and set the retrieve options variables
   if( retrieveOptions( argn, args, mainOptions ) == false ) {
      //Failed to retrieve options (Due to contradiction or an error)
      printUsageMessage(); //Prints the usage message
      return 0;
   }
   if (rank == 0 && argn < 3) {
      //Failed to retrieve options (Due to contradiction or an error)
      printUsageMessage(); //Prints the usage message
      return 0;
   }

   //Convert files
   for (size_t entryName = 0; entryName < fileList.size(); entryName++) {
      if (entryCounter++ % ntasks == rank) {
         //Get the file name
         const string & fileName = fileList[entryName];
         //Check vlsv library version
         if( checkVersion(fileName) == 1.00 ) {
            //Using new vlsv library
            convertFileToSilo<newVlsv::Reader>( fileName, mainOptions );
         } else {
            //Using old vlsv library
            convertFileToSilo<oldVlsv::Reader>( fileName, mainOptions );
         }
      }
   }
   MPI_Finalize();
   return 0;


   return 0;
}


