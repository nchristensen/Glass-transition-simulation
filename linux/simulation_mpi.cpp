/*
*This program simulates the movement of inert particles using Lennard-Jones potential.
*Force is calculated from the potential.  Position and velocity are updated using the Verlet
* Algorithm (with Euler algorithm initialization).
*/

#include <cmath>
#include <iostream>
#include <ctime>
#include <fstream>
#include <random>
#include <vector>
#include <cstring>
#include "mpi.h"

#define DIM 3
#define EMPTY_LEAF -1
#define BRANCH -2

//#define BARNES_HUT

//===================================BEGIN FUNCTION HEADERS===================================
//Determines shortest vector from particle 1 to particle 2 (including across boundary) in one direction
inline double determineVectorPeriodic(const double p1Pos, const double p2Pos, const double size)
{
    double ds = p1Pos - p2Pos;
    return (fabs(ds) > 0.5 * size) ? (ds - copysign(size, ds)) : ds;
}

inline double determineVectorFlat(const double p1Pos, const double p2Pos)
{
	return p1Pos - p2Pos;
}

//===================================END FUNCTION HEADERS=====================================

//===================================BEGIN TREE STUFF=========================================

struct Node{
	int particleIndex;
	double mass;
      double com[DIM];  
      //Run out of memory if save.
      //double boundaries[8][6];
      //double std::vector indicesArray[8];
};

class Tree{

      public:
	      std::vector<Node> nodesArray;

            Tree()
            {
                  //init_mpi_ops();
            }
      
		Tree(double (*position)[DIM], int numParticles, double boundaries[6], int rank)
		{
                  //init_mpi_ops();
			std::vector<int> indices(numParticles);
			for(int i = 0; i < numParticles; i++)				
                        indices[i] = i;		
			buildTree(0, position, indices, boundaries, rank);
		};

            Tree(std::vector<Node> &vec)
            {
                  //init_mpi_ops();
                  nodesArray = vec;
            }

      //Create vector based tree. Can split work among processes at second level.
	void buildTree(int nodeIndex, double (*position)[DIM], std::vector<int> &partIndices, double *boundaries, int rank)
	{	
            //if(rank == 0) printf("here %d\n", nodeIndex);
            //printf("here %d\n", nodeIndex);
            //printf("Node index: %d, Number of particles: %ld, Boundaries: %g %g %g %g %g %g\n", nodeIndex, partIndices.size(),
            //      boundaries[0], boundaries[1], boundaries[2],boundaries[3],boundaries[4],boundaries[5]);
                      
            if(nodeIndex >= nodesArray.size())
                 nodesArray.resize(nodeIndex + 1);

		if(partIndices.size() == 0)
		{
			nodesArray[nodeIndex].particleIndex = EMPTY_LEAF;
		}
		else if(partIndices.size() == 1)
		{
			nodesArray[nodeIndex].particleIndex = partIndices[0];
			//Assume equivalent mass, otherwise will need to modify
			nodesArray[nodeIndex].mass = 1.0;
                  memcpy(&(nodesArray[nodeIndex].com[0]), &position[partIndices[0]][0], DIM*sizeof(double));	
		}
		else
		{
			//Assuming mass of one, sum mass
                  nodesArray[nodeIndex].particleIndex = BRANCH;
			nodesArray[nodeIndex].mass = partIndices.size();

			//Assuming mass of one per particle, average location
                  //Zero out center of mass
                  memset(nodesArray[nodeIndex].com, 0, DIM*sizeof(double));
			for(int i = 0; i < partIndices.size(); i++)
			{
				for(int j = 0; j < DIM; j++)
				{
					nodesArray[nodeIndex].com[j] += position[partIndices[i]][j]; 
				}
			}
			for(int j = 0; j < DIM; j++)
				nodesArray[nodeIndex].com[j] /= partIndices.size();
			
			//Create new boundaries
			double halfX = 0.5*(boundaries[0] + boundaries[1]);
			double halfY = 0.5*(boundaries[2] + boundaries[3]);
			double halfZ = 0.5*(boundaries[4] + boundaries[5]);                
                  
                  double boundaryArray[8][6] = {{boundaries[0],halfX,boundaries[2],halfY,boundaries[4],halfZ},//bottom, front, left
                       				      {boundaries[0],halfX,boundaries[2],halfY,halfZ,boundaries[5]},
							      {boundaries[0],halfX,halfY,boundaries[3],boundaries[4],halfZ},
							      {boundaries[0],halfX,halfY,boundaries[3],halfZ,boundaries[5]},
                                                {halfX,boundaries[1],boundaries[2],halfY,boundaries[4],halfZ},
							      {halfX,boundaries[1],boundaries[2],halfY,halfZ,boundaries[5]},
							      {halfX,boundaries[1],halfY,boundaries[3],boundaries[4],halfZ},
                                                {halfX,boundaries[1],halfY,boundaries[3],halfZ,boundaries[5]}};  

			std::vector<int> indicesArray[8];               

			//Subdivide the indices based on boundaries
                  char octant;
			for(int i = 0; i < partIndices.size(); i++)
			{
                        octant = position[partIndices[i]][0] > halfX ? 4 : 0; 
                        if(position[partIndices[i]][1] > halfY) octant |= 2;
                        if(position[partIndices[i]][2] > halfZ) octant |= 1;
                        indicesArray[octant].push_back(partIndices[i]);
			}			
									
			//Build tree based on subdivisions //Can generalize to handle any power of 8 based on size and nodeIndex
                  //Could generalize to handle any 1, 2, 4, 8! Maybe more if subdivide at lower levels
                  if(nodeIndex == 0) //&& size >= 8 //Spin off new threads at first level
                  {
                        int index = rank % 8;
                        buildTree(index + 1, position, indicesArray[index], boundaryArray[index], rank);                                       
                  }
                  //Potential case for 64 processes.
                  //else if(nodeIndex > 0 && nodeIndex <= 9 && size >= 64)
                  //{}
                  else //Build 
                  {
                        for(int i = 7; i >= 0; i--) //Reduce the number of resizes by going right to left
                        {
                              buildTree(8*nodeIndex + (i + 1), position, indicesArray[i], boundaryArray[i], rank);
                        }
                  }          
		}
	}
    
      void calcAcc(int nodeIndex, int partIndex, double (*position)[DIM], double acceleration[DIM], double *potentialEnergy)
      {            
            //Empty square or itself, do nothing
            if(nodesArray[nodeIndex].particleIndex != EMPTY_LEAF && nodesArray[nodeIndex].particleIndex != partIndex)
            {
                  double vectors[DIM];
                  double pythagorean = 0;
                  for(int i = 0; i < DIM; i++)
                  {
                        vectors[i] = determineVectorFlat(position[partIndex][i], nodesArray[nodeIndex].com[i]);
                        pythagorean += vectors[i]*vectors[i];
                  }

                  double s = nodesArray[nodeIndex].mass/sqrt(pythagorean);

                  if(nodesArray[nodeIndex].particleIndex >= 0 || s < 0.5) //Calculate force
                  {
		            //Force derived from Lennard-Jones potential
                        //Guessing this should be an average of sigma values of all involved particles. Since only using type 1 just set to one here.     
		            double sigma = 1.0;//(i < particlesType1 && j < particlesType1) ? 1.0 : ((i >= particlesType1 && j >= particlesType1) ? 1.4 : 1.2);				
		            double sigmaPow6 = sigma*sigma;//Use sigmaPow6 as to hold the square temporarily
		            sigmaPow6 = sigmaPow6*sigmaPow6*sigmaPow6;
		            double sigmaPow12 = sigmaPow6*sigmaPow6;
		            double invPy = 1.0 / pythagorean;
		            double invPyPow3 = invPy*invPy*invPy;
		            double invPyPow4 = invPyPow3*invPy;
		            double invPyPow6 = invPyPow3*invPyPow3;
		            double forceCoeff = (sigmaPow6 * invPyPow4) * ((48.0 * sigmaPow6 * invPyPow3) - 24.0);				

                        for(int k = 0; k < DIM; k++)
                              acceleration[k] += vectors[k]*forceCoeff;

                        potentialEnergy[0] += 2 * ((sigmaPow12 * invPyPow6) - (sigmaPow6 * invPyPow3));
                  }
                  else //Check children
                  {
                        for(int i = 0; i < 8; i++)
                              calcAcc(8*nodeIndex + (i + 1), partIndex, position, acceleration, potentialEnergy);
                  }   
            }   
      }
      
};

//============================END TREE STUFF============================================

//============================MORE FUNCTION DECLARATIONS======================================

//Calculates force on each particle based on the present position of all particles
//Cutoff distance of 4.0 units. 
//Then calculates the acceleration of each particle based on the force
//currently applied to it.
void calcAcceleration(double (*acceleration)[DIM], double (*position)[DIM], double totalParticles,  
      int myStart, int myEnd,	double particlesType1, double *potentialEnergy, double boundaries[6]);

//Barnes-Hut version
void calcAcceleration(double (*acceleration)[DIM], double (*position)[DIM], int totalParticles, int localParticles,
      int myStart, int numClusters, int clusterID, int rank, double particlesType1,
      double *potentialEnergy, double boundaries[6], Tree *tree, std::vector<int> &indices);

//Function that performs the Euler algorithm on all particles in the set
void performEulerOperation(int totalParticles, int localParticles, int myStart, int myEnd,
      int clusterStart, int clusterEnd, int rank,
      double (*position)[DIM], int particlesType1, double energies[2], double boundaries[6],
      double (*oldPosition)[DIM], double (*acceleration)[DIM], double (*velocity)[DIM],
      double timestep, Tree *tree, std::vector<int> &indices);

//Function that performs the Verlet algorithm on all particles in the set
void performVerletOperation(int totalParticles, int localParticles, int myStart, int myEnd, 
      int clusterStart, int clusterEnd, int rank,
      double (*position)[DIM], int particlesType1, double energies[2],
	double boundaries[6], double (*oldPosition)[DIM], double (*acceleration)[DIM],
	double (*velocity)[DIM], double timestep, double halfInvTimestep,
      Tree *tree, std::vector<int> &indices);

//Translates particles that have exited the simulation area back into the 
//Simulation region, works in one direction so do for every direction
void applyPeriodicBoundary(double &position, double &oldPosition, double *boundary);

//Bounce particle off wall
void applySolidBoundary(double &position, double &oldPosition, double *boundary);

//Outputs to position file
void outputPosition(std::ofstream &positionFile, const double currentTime, double (*position)[DIM], const int totalParticles);

//Initializes 
int initializeParticles(double (*position)[DIM], double (*velocity)[DIM], 
      int totalParticles, double boundaries[6]);

//Deletes arrays and closes files
void cleanup(double (*position)[DIM], double (*oldPosition)[DIM],
      double (*velocity)[DIM], double (*acceleration)[DIM]);

//=======================================================================================

MPI_Comm COMM_COLUMN;

//The main function to execute the simulation
int main(int argc, char* argv[])
{
      //Setup MPI
      int rank, size;

      MPI_Init(&argc, &argv);

      MPI_Comm_size(MPI_COMM_WORLD, &size);
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);

      #ifdef BARNES_HUT
      if(size % 8 != 0)
      {            
            printf("Error: Number of processes is not a multiple of 8\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
      }
      #endif

      //START SETUP
	clock_t tstart = clock();
	/*CONSTANTS FOR REFERENCE THESE ARE HARDCODED*/
	//constants 0.5(sigma1 + sigma2)
	//double sigma1to1 = 1; //in units of sigma
	//double sigma2to2 = 1.4; //in units of sigma
	//double sigma1to2 = 1.2; // in units of sigma
	//double epsilon = 1;// in units of epsilon
	//double massParticle1 = 1; //in units of massParticle1
	//double massParticle2 = 2; //in units of massParticle1


      //Set time related variables
	double timestep = 0.005; //Can be arbitrarily small
	double maxTime = 10; //Can be arbitrarily long or short
	double halfInvTimestep = 0.5/timestep; //needed for Verlet
	double currentTime = 0;

      //Set variables for number of particles
	int numParticlesType1 = 1024;
	int numParticlesType2 = 0;
	int totalParticles = numParticlesType1 + numParticlesType2;

      //Set MPI variables
	int localParticles = totalParticles/size; 
	int myStart = rank*localParticles;
	int myEnd = (rank + 1)*localParticles;
      int numClusters = std::max(1, size/8);
      int clusterID = rank/8;
      int clusterParticles = totalParticles / numClusters;     
      int clusterStart = clusterParticles*clusterID;
      int clusterEnd = clusterParticles*(clusterID + 1);

      MPI_Comm_split(MPI_COMM_WORLD, clusterID, rank, &COMM_COLUMN);

      //Set energy variables
	double energies[2], totalEnergy; //{kineticEnergy, potentialEnergy}

      //Set boundary variables
	double boundaries[6] = {0.0, 50.0, 0.0, 50.0, 0.0, 50.0}; //{x_min, x_max, y_min, y_max, z_min, z_max}

	//Particle information arrays
	double (*position)[DIM] = new double[totalParticles][DIM];
	double (*velocity)[DIM] = new double[totalParticles][DIM];
	double (*oldPosition)[DIM] = new double[totalParticles][DIM];
	double (*acceleration)[DIM] = new double[totalParticles][DIM];

      int errorStatus = 0;

      if(rank == 0)
      {
            errorStatus = initializeParticles(position, velocity, totalParticles, boundaries);
      }

      MPI_Bcast(&errorStatus, 1, MPI_INT, 0, MPI_COMM_WORLD);

      if(errorStatus != 0)
      {
		cleanup(position, velocity, acceleration, oldPosition);
            MPI_Abort(MPI_COMM_WORLD, 1);
      }

	//-----------------------------------------------------------------------//
	// Distribute Data           						             //
	MPI_Bcast(position, 3*totalParticles, MPI_DOUBLE, 0, MPI_COMM_WORLD);	 //
	MPI_Bcast(velocity, 3*totalParticles, MPI_DOUBLE, 0, MPI_COMM_WORLD);	 //
	//-----------------------------------------------------------------------//

	//Create data files	
	std::ofstream positionFile, energyFile;
      if(rank == 0)
      {
	      positionFile.open("position.txt");
	      energyFile.open("energy.txt");
      }
      
      Tree tree;
      //tree.init_mpi_ops();
      std::vector<int> indices(totalParticles);
      for(int i = 0; i < totalParticles; i++)
            indices[i] = i;  

      //END SETUP

      //START SIMULATION

	//Perform initial Euler operation to set things in motion

	performEulerOperation(totalParticles, localParticles, myStart, myEnd, clusterStart, 
            clusterEnd, rank, position, numParticlesType1, energies, boundaries, oldPosition,
            acceleration, velocity, timestep, &tree, indices);
    
	//Main loop - performing Verlet operations for the remainder of the simulation
	unsigned count = 0;
	for (currentTime = 2*timestep; currentTime < maxTime; currentTime += timestep)
	{
		performVerletOperation(totalParticles, localParticles, myStart, myEnd, 
                  clusterStart, clusterEnd, rank,
                  position, numParticlesType1, energies, boundaries,
                  oldPosition, acceleration, velocity, timestep, halfInvTimestep,
                  &tree, indices);

            //output
		count++;  //Can set print interval arbitrarily
		if(count >= 10)
		{
                  count = 0;
		
                  if(rank == 0) 
                  {                 
                        MPI_Reduce(MPI_IN_PLACE, energies, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
                        totalEnergy = energies[0] + energies[1];
                        
                        printf("%g %g %g %g\n", currentTime, energies[0], energies[1], totalEnergy);
			      outputPosition(positionFile, currentTime, position, totalParticles);
			      energyFile << currentTime << " " << totalEnergy << " " << energies[0]
                        << " " << energies[1] << "\n";
                  }
                  else
                  {
                        MPI_Reduce(energies, nullptr, 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);	
                  }
		}
	}     
      
      if(rank == 0)
      {
	      printf("%g\n", (double)(clock() - tstart) / CLOCKS_PER_SEC); 
	      positionFile.close();
	      energyFile.close();
      }
	cleanup(position, velocity, acceleration, oldPosition);

	return 0;
}
//End of main function

//O(n^2) force calculation
void calcAcceleration(double (*acceleration)[DIM], double (*position)[DIM], double totalParticles, 
      int myStart, int myEnd, double particlesType1, double *potentialEnergy, double boundaries[6])
{
	double sigma, sigmaPow6, sigmaPow12;
	double pythagorean, invPy, invPyPow3, invPyPow4, invPyPow6;
	double vectors[DIM], forceCoeff;
	potentialEnergy[0] = 0;
      int i, j, k;
	
	//Zero out acceleration, can't do this inside the main loop because we also access the jth entry
      memset(&acceleration[myStart], 0, DIM*(myEnd - myStart)*sizeof(double));	

	for (i = myStart; i < myEnd; i++)
	{
		for (j = 0; j < i; j++)
		{
                  pythagorean = 0;
                  for(k = 0; k < DIM; k++)
                  {
                        vectors[k] = determineVectorFlat(position[i][k], position[j][k]);
                        //vectors[k] = determineVectorPeriodic(position[i][k], position[j][k], boundaries[2*k + 1]);
                        pythagorean += vectors[k]*vectors[k];
                  }

			if (pythagorean < 16.0)
			{
				//Force derived from Lennard-Jones potential
				sigma = 1.0;//(i < particlesType1 && j < particlesType1) ? 1.0 : ((i >= particlesType1 && j >= particlesType1) ? 1.4 : 1.2);				
				sigmaPow6 = sigma*sigma;//Use sigmaPow6 to hold the square temporarily
				sigmaPow6 = sigmaPow6*sigmaPow6*sigmaPow6;
				sigmaPow12 = sigmaPow6*sigmaPow6;
				invPy = 1.0 / pythagorean;
				invPyPow3 = invPy*invPy*invPy;
				invPyPow4 = invPyPow3*invPy;
				invPyPow6 = invPyPow3*invPyPow3;
				forceCoeff = (sigmaPow6 * invPyPow4) * ((48.0 * sigmaPow6 * invPyPow3) - 24.0);				

                        for(k = 0; k < DIM; k++)
                        {
                              acceleration[i][k] += vectors[k]*forceCoeff;
                        }

                        potentialEnergy[0] += 2 * ((sigmaPow12 * invPyPow6) - (sigmaPow6 * invPyPow3));
			}
		}
		for (j = i + 1; j < totalParticles; j++)
		{
                  pythagorean = 0;
                  for(k = 0; k < DIM; k++)
                  {
                        vectors[k] = determineVectorFlat(position[i][k], position[j][k]);
                        //vectors[k] = determineVectorPeriodic(position[i][k], position[j][k], boundaries[2*k + 1]);
                        pythagorean += vectors[k]*vectors[k];
                  }

			if (pythagorean < 16.0)
			{
				//Force derived from Lennard-Jones potential
				sigma = 1.0;//(i < particlesType1 && j < particlesType1) ? 1.0 : ((i >= particlesType1 && j >= particlesType1) ? 1.4 : 1.2);				
				sigmaPow6 = sigma*sigma;//Use sigmaPow6 to hold the square temporarily
				sigmaPow6 = sigmaPow6*sigmaPow6*sigmaPow6;
				sigmaPow12 = sigmaPow6*sigmaPow6;
				invPy = 1.0 / pythagorean;
				invPyPow3 = invPy*invPy*invPy;
				invPyPow4 = invPyPow3*invPy;
				invPyPow6 = invPyPow3*invPyPow3;
				forceCoeff = (sigmaPow6 * invPyPow4) * ((48.0 * sigmaPow6 * invPyPow3) - 24.0);				

                        for(k = 0; k < DIM; k++)
                        {
                              acceleration[i][k] += vectors[k]*forceCoeff;
                        }

                        potentialEnergy[0] += 2 * ((sigmaPow12 * invPyPow6) - (sigmaPow6 * invPyPow3));
			}
		}
	}

	//Because type two particles are twice as heavy
	//for (i = particlesType1; i < totalParticles; ++i)
      //      for(j = 0; j < DIM; j++)
      //            acceleration[i][j] *= 0.5;
}

void calcAcceleration(double (*acceleration)[DIM], double (*position)[DIM], int totalParticles,
     int localParticles, int myStart, int clusterStart, int clusterEnd,
     int rank, double particlesType1,
     double *potentialEnergy, double boundaries[6], Tree *tree, std::vector<int> &indices)
{                             
      //Set acceleration of cluster particles to zero, this is legal 
      //http://stackoverflow.com/questions/4629853/is-it-legal-to-use-memset-0-on-array-of-doubles
      memset(&acceleration[clusterStart], 0, DIM*(clusterEnd - clusterStart)*sizeof(double));
      //Zero out potential energy
      potentialEnergy[0] = 0;

      tree->buildTree(0, position, indices, boundaries, rank);

      for(int i = clusterStart; i < clusterEnd; i++)
            tree->calcAcc(rank % 8 + 1, i, position, acceleration[i], potentialEnergy);

      //printf("%d %d\n", rank, clusterStart);
      MPI_Reduce_scatter_block(MPI_IN_PLACE, &acceleration[clusterStart], 
            3*localParticles, MPI_DOUBLE, MPI_SUM, COMM_COLUMN);          
}

//Function that performs the Euler algorithm on all particles in the set
void performEulerOperation(int totalParticles, int localParticles, int myStart, int myEnd,
      int clusterStart, int clusterEnd, int rank,
      double (*position)[DIM], int particlesType1, double energies[2], double boundaries[6],
      double (*oldPosition)[DIM], double (*acceleration)[DIM], double (*velocity)[DIM],
      double timestep, Tree *tree, std::vector<int> &indices)
{
      #ifdef BARNES_HUT
	calcAcceleration(acceleration, position, totalParticles, localParticles, myStart, clusterStart,
          clusterEnd, rank, particlesType1, &energies[1], boundaries, tree, indices);
      #else
      calcAcceleration(acceleration, position, totalParticles, myStart, myEnd, particlesType1, &energies[1], boundaries);
      #endif

      int accStart = clusterStart - myStart;
	double dotProd;

	int j;
	energies[0] = 0;
	for (int i = myStart; i < myEnd; i++)
	{
		dotProd = 0.0;
		for (j = 0; j < DIM; ++j)
		{
			oldPosition[i][j] = position[i][j];
			position[i][j] += (velocity[i][j] * timestep);
                  #ifdef BARNES_HUT
                  velocity[i][j] += acceleration[accStart + i][j] * timestep;
                  #else
			velocity[i][j] += (acceleration[i][j] * timestep);
                  #endif

                  applySolidBoundary(position[i][j], oldPosition[i][j], &boundaries[2*j]);
                  //applyPeriodicBoundary(position[i][j], oldPosition[i][j], boundaries[2*j]);
			dotProd += velocity[i][j] * velocity[i][j];
		}

		energies[0] += (i < particlesType1) ? 0.5 * dotProd : dotProd;
	}

	MPI_Allgather(MPI_IN_PLACE, 3*localParticles, MPI_DOUBLE, position,
	      3*localParticles, MPI_DOUBLE, MPI_COMM_WORLD);
}

void performVerletOperation(int totalParticles, int localParticles, int myStart, int myEnd, 
      int clusterStart, int clusterEnd, int rank,
      double (*position)[DIM], int particlesType1, double energies[2],
	double boundaries[6], double (*oldPosition)[DIM], double (*acceleration)[DIM],
	double (*velocity)[DIM], double timestep, double halfInvTimestep,
      Tree *tree, std::vector<int> &indices)
{
      #ifdef BARNES_HUT
	calcAcceleration(acceleration, position, totalParticles, localParticles, myStart,
          clusterStart, clusterEnd, rank, particlesType1, &energies[1], boundaries, tree, indices);
      #else
      calcAcceleration(acceleration, position, totalParticles, myStart, myEnd, particlesType1, &energies[1], boundaries);
      #endif

	double currentPosition;
	double currentDisplacement;
	double futureDisplacement;
	double dotProd;
      double dtsq = timestep*timestep;
	int j;
      int accStart = clusterStart - myStart;
	energies[0] = 0;
  

      for (int i = myStart; i < myEnd; i++)
	{
		// Vector Verlet Method, unroll the loops? Compiler takes care of?
		dotProd = 0;
        	for(j = 0; j < DIM; ++j) //Loop over all directions
		{
			currentDisplacement = position[i][j] - oldPosition[i][j];
                  #ifdef BARNES_HUT
                  futureDisplacement = currentDisplacement + (dtsq * acceleration[accStart + i][j]);
                  #else
			futureDisplacement = currentDisplacement + (dtsq * acceleration[i][j]);
                  #endif
			currentPosition = position[i][j];
			position[i][j] = position[i][j] + futureDisplacement;
			velocity[i][j] = (position[i][j] - oldPosition[i][j]) * (halfInvTimestep);
			oldPosition[i][j] = currentPosition;

                  applySolidBoundary(position[i][j], oldPosition[i][j], &boundaries[2*j]);
                  //applyPeriodicBoundary(position[i][j], oldPosition[i][j], boundaries[2*j]);
			dotProd += velocity[i][j] * velocity[i][j];
		}

		energies[0] += (i < particlesType1) ? 0.5 * dotProd : dotProd;
	}

	MPI_Allgather(MPI_IN_PLACE, 3*localParticles, MPI_DOUBLE, position,
		3*localParticles, MPI_DOUBLE, MPI_COMM_WORLD);
}

//CHANGE THESE FUNCTIONS IF WANT TO SUPPORT NON-ZERO LOWER BOUND
void applyPeriodicBoundary(double &position, double &oldPosition, double *boundary)
{
	if(position < boundary[0])
	{
            double size = boundary[1] - boundary[0];
		position += size;
		oldPosition += size;
	}
  	else if(position > boundary[1])
	{
            double size = boundary[1] - boundary[0];
		position -= size;
		oldPosition -= size;
	}
}

void applySolidBoundary(double &position, double &oldPosition, double *boundary)
{
	//If pass in both axis boundaries, could do this with one if statement
	if(position < boundary[0])
	{
		position -= 2*(position - boundary[0]);
		oldPosition -= 2*(oldPosition - boundary[0]);
	}
	else if(position > boundary[1])
	{
		position -= 2*(position - boundary[1]);
		oldPosition -= 2*(oldPosition - boundary[1]);
	}
}

//Can likely offload this to a separate thread - Otherwise would be most efficient to do in Verlet/Euler step 
//Since the values would be in the cache.
void outputPosition(std::ofstream &positionFile, const double currentTime, double (*position)[DIM], const int totalParticles)
{	
      std::string str;
      //Fill out remaining zeros
      int i, j;
      for(i = 0; i < 3 - DIM; ++i)
            str.append("0 ");
      str.append("0 0 0 0 0 0 0 0 0\n");
	positionFile << "* " << currentTime << "\n";
	for (i = 0; i < totalParticles; ++i)
      {
            for(j = 0; j < DIM; ++j)
                  positionFile << position[i][j] << " ";
            positionFile << str;
      }
}

int initializeParticles(double (*position)[DIM], double (*velocity)[DIM], int totalParticles, double boundaries[6])
{
	std::default_random_engine generator((unsigned)time(NULL));
	std::normal_distribution<double> normalDistribution(0.0, 0.5);
      std::uniform_real_distribution<double> uniformDistribution[DIM];
      for(int i = 0; i < DIM; i++)
            uniformDistribution[i] = std::uniform_real_distribution<double>(boundaries[2*i], boundaries[2*i + 1]);
	
	for(int i = 0; i < totalParticles; i++)
            for(int j = 0; j < DIM; j++)
                  velocity[i][j] = normalDistribution(generator);

	double minSpacing = 1.0;//Be careful with this parameter, energy is really high when this is small
	double maxIter = 50;
	double norm;
	int oldi = 0;
	unsigned count = 0;
	double dist;

	//Loop for a set number of iterations or until no particles have to be moved
	for(int i = 0; i < totalParticles && count < maxIter; i++)
	{		
		count = (i != oldi) ? 0 : count + 1; //If this is a particle we've already moved, add one to count
		oldi = i;
            for(int j = 0; j < DIM; j++)
                  position[i][j] = uniformDistribution[j](generator);
	
		for(int j = 0; j < i; j++)
		{
			norm = 0;
			for(int k = 0; k < DIM; k++)
			{	
				//dist = determineVectorPeriodic(position[i][k], position[j][k], boundaries[k]);
                        dist = determineVectorFlat(position[i][k], position[j][k]);
				norm += dist*dist;
			}
			norm = sqrt(norm);
			
			if(norm < minSpacing)
			{
				//Start over checking ith particle against prior particles
				i--;
				break; 
			}
		}
	}

	if(count >= maxIter)
	{
		printf("Maximum iteration reached when attempting to place particles. Try using a larger domain.\n");
		return 1;
	}

      return 0;
}

void cleanup(double (*position)[DIM], double (*oldPosition)[DIM], 
             double (*velocity)[DIM], double (*acceleration)[DIM])
{
	delete [] position;
      delete [] velocity;
      delete [] acceleration;
      delete [] oldPosition;
}
