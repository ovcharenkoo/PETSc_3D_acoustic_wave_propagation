
/*
  Author: Mohammed A. Al Farhan, PhD student at ECRC, KAUST
  Email:  mohammed.farhan@kaust.edu.sa

  ps4.c: A PETSc example that solves a 2D linear Poisson equation

  The example code is based upon:
    1) "PETSC FOR PARTIAL DIFFERENTIAL EQUATIONS" book by
        Professor ED BUELER:
        https://github.com/bueler/p4pdes/releases
    2) PETSc KSP Example 50 (ex50):
        $PETSC_DIR/src/ksp/ksp/examples/tutorials/ex50.c
    3) PETSc KSP Example 15 (ex15):
        $PETSC_DIR/src/ksp/ksp/examples/tutorials/ex15.c
*/

#include <stdio.h>
#include <petscdmda.h>
#include <petscksp.h>

/*
  User-functions prototypes
*/
PetscErrorCode
compute_rhs(KSP, Vec, void *);
PetscErrorCode
compute_opt(KSP, Mat, Mat, void *);
PetscErrorCode
test_convergence_rate(KSP, Vec);

/*
  Main C function
*/
int
main(int argc, char * args[]) 
{
  PetscErrorCode ierr; // PETSc error code

  // Initialize the PETSc database and MPI
  // "petsc.opt" is the PETSc database file
  ierr = PetscInitialize(&argc, &args, NULL, NULL);
  CHKERRQ(ierr); // PETSc error handler

  // The global PETSc MPI communicator
  MPI_Comm comm = PETSC_COMM_WORLD;
  /*
    PETSc DM
    Create a default 16x16 2D grid object
    The minus sign means that the grid x and y dimensions
    are changeable through the command-line options
  */
  DM da;

    ierr = DMDACreate3d(comm, DM_BOUNDARY_GHOSTED, DM_BOUNDARY_GHOSTED, DM_BOUNDARY_GHOSTED,
                      DMDA_STENCIL_STAR, -17, -17, -17, PETSC_DECIDE, PETSC_DECIDE,
                      PETSC_DECIDE, 1, 1, NULL, NULL, NULL, &da);

  CHKERRQ(ierr);

  /*
    PETSc Vec
  */
  /*  Vector of unknowns approximating \varphi_{i,j}
      on the grid */
  Vec u;
  // Create a global vector derived from the DM object
  // "Global" means "distributed" in MPI language
  ierr = DMCreateGlobalVector(da, &u);
  CHKERRQ(ierr);

  /* 
    The right-hand side vector approximating the values of f_{i,j}
  */
  Vec b;
  // Duplicate creates a new vector of the same type as u
  ierr = VecDuplicate(u, &b);
  CHKERRQ(ierr);


//  PetscViewerSocketOpen(PETSC_COMM_WORLD, NULL, PETSC_DEFAULT, PetscViewer &viewer);

  /* 
    Krylov Subspace (KSP) object to solve the linear system
  */
  KSP ksp;
  // Create the KPS object
  ierr = KSPCreate(comm, &ksp);
  CHKERRQ(ierr);
  // Set the DM to be used as preconditioner
  ierr = KSPSetDM(ksp, (DM) da);
  CHKERRQ(ierr);
  // Compute the right-hand side vector b
  ierr = KSPSetComputeRHS(ksp, compute_rhs, NULL);
  CHKERRQ(ierr);
  // Compute and assemble the coefficient matrix A
  ierr = KSPSetComputeOperators(ksp, compute_opt, NULL);
  CHKERRQ(ierr);
  // KSP options can be changed during the runtime
  ierr = KSPSetFromOptions(ksp);
  CHKERRQ(ierr);
  // Solve the linear system using KSP
  ierr = KSPSolve(ksp, b, u);
  CHKERRQ(ierr);

  // Verifies the implementation by comparing the
  // numerical solution to the analytical solution
  // The function computes a norm of the difference
  // between the computed solution and the exact solution
  ierr = test_convergence_rate(ksp, u);
  CHKERRQ(ierr);

  /*
    Cleanup the allocations, and exit
  */
  ierr = VecDestroy(&u);
  CHKERRQ(ierr);
  ierr = VecDestroy(&b);
  CHKERRQ(ierr);
  ierr = KSPDestroy(&ksp);
  CHKERRQ(ierr);
  ierr = DMDestroy(&da);
  CHKERRQ(ierr);

  // Exit the MPI communicator and finalize the PETSc
  // initialization objects
  ierr = PetscFinalize();
  CHKERRQ(ierr);

  return 0;
}

/*
  Compute the right-hand side vector
*/
PetscErrorCode
compute_rhs(KSP ksp, Vec b, void * ctx) 
{
  PetscFunctionBegin;

  PetscErrorCode ierr;

  /* Get the DM oject of the KSP */
  DM da;
  ierr = KSPGetDM(ksp, &da);
  CHKERRQ(ierr);

  /* Get the global information of the DM grid*/
  DMDALocalInfo grid;
  ierr = DMDAGetLocalInfo(da, &grid);
  CHKERRQ(ierr);

  /* Grid spacing */
  double hx = (1.f / (double) (grid.mx - 1));
  double hy = (1.f / (double) (grid.my - 1));
  double hz = (1.f / (double) (grid.mz - 1));

  /*  A pointer to access b, the right-hand side PETSc vector
      viewed as a C array */
  double *** _b;
  ierr = DMDAVecGetArray(da, b, &_b);
  CHKERRQ(ierr);

  /*
    Loop over the grid points, and fill b 
  */
  unsigned int k;
  for(k = grid.zs; k < (grid.zs + grid.zm); k++) // Depth
  {
    double z = k * hz; // Z

    unsigned int j;
    for(j = grid.ys; j < (grid.ys + grid.ym); j++) // Columns
    {
      double y = j * hy; // Y

      unsigned int i;
      for(i = grid.xs; i < (grid.xs + grid.xm); i++) // Rows
      {
        double x = i * hx; // X

        /* Nodes on the boundary layers (\Gamma) */
        if((i == 0) || (i == (grid.mx - 1)) ||
          (j == 0) || (j == (grid.my - 1)) ||
          (k == 0) || (k == (grid.mz - 1)))
        {
          _b[k][j][i] = 0.f;
        }
        /* Interior nodes in the domain (\Omega) */
        else 
        { 
          double x2 = x * x; // x^2
          double y2 = y * y; // y^2
          double z2 = z * z; // y^2
          double x4 = x2 * x2;
          double y4 = y2 * y2;
          double z4 = z2 * z2;

          double f = hx * hy * hz; // Scaling

          f *=  2.f * (-y2 * (-1.f + y2) * z2 * (-1.f + z2)  + 
                x4 * (z2 - z4 + y4 * (-1.f + 6.f * z2) + y2 * (1.f - 12.f * z2 + 6.f * z4)) + 
                x2 * (z2 * (-1.f + z2) + y2 * (-1.f + 18.f * z2 - 12.f * z4) + y4 * (1.f - 12.f * z2 + 6.f * z4)));

          // if ((i==10) && (j==10) && (k==10)) 
          // {
          //     f *= 10.f;
          // }
          // else
          // {
          //     f *= 0.f;
          // }
          _b[k][j][i] = f;
        }
      }
    }
  }

  // Release the resource
  ierr = DMDAVecRestoreArray(da, b, &_b);
  CHKERRQ(ierr);

  // VecView(b, PetscViewer viewer);

  PetscFunctionReturn(0);
}

/* Compute the operator matrix A */
PetscErrorCode 
compute_opt(KSP ksp, Mat A, Mat J, void * ctx)
{
  PetscFunctionBegin;

  PetscErrorCode ierr;

  // Get the DMDA object
  DM da;
  ierr = KSPGetDM(ksp, &da);
  CHKERRQ(ierr);

  // Get the grid information
  DMDALocalInfo grid;
  ierr = DMDAGetLocalInfo(da, &grid);
  CHKERRQ(ierr);

  /*  A PETSc data structure to store information
      about a single row or column in the stencil */
  MatStencil idxm;
  MatStencil idxn[7];

/*=========================================================================================
 Using example 34 from
 http://www.mcs.anl.gov/petsc/petsc-current/src/ksp/ksp/examples/tutorials/ex34.c.html \
  =========================================================================================*/

  // The matrix values
  PetscScalar v[7], hx, hy, hz, hyhzdhx, hxhzdhy, hxhydhz;
  PetscInt num, numi, numj, numk;

  hx = (1.f / (double) (grid.mx - 1));
  hy = (1.f / (double) (grid.my - 1));
  hz = (1.f / (double) (grid.mz - 1));
  hyhzdhx = hy * hz / hx;
  hxhzdhy = hx * hz / hy;
  hxhydhz = hx * hy / hz;

  /* Loop over the grid points */
  unsigned int k;
  for(k = grid.zs; k < (grid.zs + grid.zm); k++) // Depth 
  {
    unsigned int j;
    for(j = grid.ys; j < (grid.ys + grid.ym); j++) // Columns 
    {
      unsigned int i;
      for(i = grid.xs; i < (grid.xs + grid.xm); i++)  // Rows
      {
        idxm.k = k;
        idxm.j = j;
        idxm.i = i;
        
        idxn[0].k = k;
        idxn[0].j = j;
        idxn[0].i = i;

        /* Nodes on the boundary layers (\Gamma) */
        if((i == 0) || (i == (grid.mx - 1)) ||
          (j == 0) || (j == (grid.my - 1)) ||
          (k == 0) || (k == (grid.mz - 1)))
        {
          num = 0; numi=0; numj=0; numk=0;
          if (k!=0) {
              v[num]     = - hxhydhz;
              // v[num] = 1;
              idxn[num].i = i;
              idxn[num].j = j;
              idxn[num].k = k-1;
              num++; numk++;
            }
            if (j!=0) {
              v[num]     = - hxhzdhy;
              // v[num] = 1;
              idxn[num].i = i;
              idxn[num].j = j-1;
              idxn[num].k = k;
              num++; numj++;
              }
            if (i!=0) {
              v[num]     = - hyhzdhx;
              // v[num] = 1;
              idxn[num].i = i-1;
              idxn[num].j = j;
              idxn[num].k = k;
              num++; numi++;
            }
            if (i!=grid.mx-1) {
              v[num]     = - hyhzdhx;
              // v[num] = 1;
              idxn[num].i = i+1;
              idxn[num].j = j;
              idxn[num].k = k;
              num++; numi++;
            }
            if (j!=grid.my-1) {
              v[num]     = - hxhzdhy;
              // v[num] = 1;
              idxn[num].i = i;
              idxn[num].j = j+1;
              idxn[num].k = k;
              num++; numj++;
            }
            if (k!=grid.mz-1) {
              v[num]     = - hxhydhz;
              // v[num] = 1;
              idxn[num].i = i;
              idxn[num].j = j;
              idxn[num].k = k+1;
              num++; numk++;
            }
            v[num] = ((PetscReal)(numk)*hxhydhz + (PetscReal)(numj)*hxhzdhy + (PetscReal)(numi)*hyhzdhx);
            
            idxn[num].i = i;
            idxn[num].j = j;   
            idxn[num].k = k;
            
            num++;
            ierr = MatSetValuesStencil(A,1,(const MatStencil *) &idxm, (Petsc64bitInt) num, (const MatStencil *) &idxn,(PetscScalar *) v, INSERT_VALUES);    
            CHKERRQ(ierr); 
        }
        /* Interior nodes in the domain (\Omega) */
        else 
        {
           v[0] = -hxhydhz;                            idxn[0].i = i;   idxn[0].j = j;   idxn[0].k = k-1;
           v[1] = -hxhzdhy;                            idxn[1].i = i;   idxn[1].j = j-1; idxn[1].k = k;
           v[2] = -hyhzdhx;                            idxn[2].i = i-1; idxn[2].j = j;   idxn[2].k = k;
           v[3] = 2.f*(hyhzdhx + hxhzdhy + hxhydhz); idxn[3].i = i;   idxn[3].j = j;   idxn[3].k = k;
           v[4] = -hyhzdhx;                            idxn[4].i = i+1; idxn[4].j = j;   idxn[4].k = k;
           v[5] = -hxhzdhy;                            idxn[5].i = i;   idxn[5].j = j+1; idxn[5].k = k;
           v[6] = -hxhydhz;                            idxn[6].i = i;   idxn[6].j = j;   idxn[6].k = k+1;
           ierr = MatSetValuesStencil(A,1,(const MatStencil *) &idxm, (Petsc64bitInt) 7, (const MatStencil *) &idxn,(PetscScalar *) v, INSERT_VALUES);
           CHKERRQ(ierr);  
        }
      }
    }
  }
  
  /* Assemble the matrix */
  ierr = MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY);
  CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A ,MAT_FINAL_ASSEMBLY);
  CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

/* Test the convergence rate */
PetscErrorCode
test_convergence_rate(KSP ksp, Vec u)
{
  PetscFunctionBegin;

  PetscErrorCode ierr;

  // Get the DMDA object
  DM da;
  ierr = KSPGetDM(ksp, &da);
  CHKERRQ(ierr);

  // Get the grid information
  DMDALocalInfo grid;
  ierr = DMDAGetLocalInfo(da, &grid);
  CHKERRQ(ierr);

  // Create a global vector
  Vec u_;
  ierr = VecDuplicate(u, &u_);
  CHKERRQ(ierr);

  double *** _u;

  double hx = (1.f / (double) (grid.mx - 1));
  double hy = (1.f / (double) (grid.my - 1));
  double hz = (1.f / (double) (grid.mz - 1));

  // Get a pointer to the PETSc vector
  ierr = DMDAVecGetArray(da, u_, &_u);
  CHKERRQ(ierr);
  
  unsigned int k;
  for(k = grid.zs; k < (grid.zs + grid.zm); k++)
  {
    double z = k * hz;

    unsigned int j;
    for(j = grid.ys; j < (grid.ys + grid.ym); j++)
    {
      double y = j * hy;

      unsigned int i;
      for(i = grid.xs; i < (grid.xs + grid.xm); i++)
      {
        double x = i * hx;

        double x2 = x * x;
        double y2 = y * y;
        double z2 = z * z;

        _u[k][j][i] = (x2 - x2 * x2) * (y2 * y2 - y2) * (z2 - z2 * z2);
      }
    }
  }

  ierr = DMDAVecRestoreArray(da, u_, &_u);
  CHKERRQ(ierr);

  double val = 0.f;

  ierr = VecAXPY(u, -1.f, u_);
  CHKERRQ(ierr);
  ierr = VecNorm(u, NORM_INFINITY, (PetscScalar *) &val);
  CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,
                    "Numerical Error [NORM_INFINITY]: \t %g\n", val);
  CHKERRQ(ierr);

  ierr = VecDestroy(&u_); CHKERRQ(ierr);

  return 0;
}
