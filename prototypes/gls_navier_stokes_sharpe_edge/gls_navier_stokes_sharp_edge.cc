﻿/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2000 - 2016 by the deal.II authors
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the deal.II distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Wolfgang Bangerth, University of Heidelberg, 2000
 */


// @sect3{Include files}

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/logstream.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/block_sparse_matrix.h>

#include <deal.II/lac/precondition.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/lac/affine_constraints.h>
#include <deal.II/lac/sparse_direct.h>

#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/solver_minres.h>
#include <deal.II/lac/solver_fire.h>

#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/manifold_lib.h>
#include <deal.II/grid/grid_in.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_refinement.h>


#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/solution_transfer.h>


#include <deal.II/lac/trilinos_parallel_block_vector.h>
#include <deal.II/lac/trilinos_precondition.h>
#include <deal.II/lac/trilinos_solver.h>
#include <deal.II/lac/trilinos_sparse_matrix.h>
#include <deal.II/lac/trilinos_vector.h>

#include <deal.II/grid/grid_tools.h>

#include "exactsolutions.h"
#include "forcingfunctions.h"
#include "boundaryconditions.h"

#include <deal.II/base/timer.h>
#include <fstream>
#include <iostream>

// Finally, this is as in previous programs:
using namespace dealii;

enum SimulationCases{MMS=0, TaylorCouette=1,};

template <int dim>
class DirectSteadyNavierStokes
{

public:
    DirectSteadyNavierStokes(const unsigned int degreeVelocity, const unsigned int degreePressure);
    ~DirectSteadyNavierStokes();
    void runMMS();
    void runCouette();
    void runCouette_sharp();

    Function<dim> *exact_solution;
    Function<dim> *forcing_function;

private:
    void make_cube_grid(int refinementLevel);
    void refine_grid();
    void refine_mesh();
    void sharp_edge();
    void sharp_edge_V2(const bool initial_step);
    void refine_mesh_uniform();
    void setup_dofs();
    void initialize_system();
    void torque();

    void vertices_cell_mapping();

    void assemble(const bool initial_step,
                   const bool assemble_matrix);
    void assemble_system(const bool initial_step);
    void assemble_rhs(const bool initial_step);
    void solve(bool initial_step);
    void solve_2(bool initial_step);
    void calculateL2Error();
    void output_results(const unsigned int cycle) const;
    void newton_iteration(const double tolerance,
                          const unsigned int max_iteration,
                          const bool is_initial_step,
                          const bool output_result);


    std::vector<types::global_dof_index> dofs_per_block;

    double viscosity_;
    double radius;
    double radius_2;
    double speed;
    bool couette;
    bool pressure_link;
    const unsigned int           degreeIntegration_;
    Triangulation<dim> triangulation;

    FESystem<dim> fe;
    DoFHandler<dim> dof_handler;



    AffineConstraints<double>    zero_constraints;
    AffineConstraints<double>    nonzero_constraints;

    BlockSparsityPattern         sparsity_pattern;
    BlockSparseMatrix<double>    system_matrix;
    SparseMatrix<double>    pressure_mass_matrix;

    BlockVector<double>          present_solution;
    BlockVector<double>          newton_update;
    BlockVector<double>          system_rhs;
    BlockVector<double>          evaluation_point;
    BlockVector<double>          last_vect;


    Vector<double> immersed_x;
    Vector<double> immersed_y;
    Vector<double> immersed_value;
    std::vector<std::vector<typename DoFHandler<dim>::active_cell_iterator>> vertices_to_cell;


    const SimulationCases simulationCase_=MMS;
    const bool stabilized_=false;
    const bool iterative_=false;
    std::vector<double> L2ErrorU_;
    const int initialSize_=4;
    TimerOutput monitor;
};



template<int dim>
DirectSteadyNavierStokes<dim>::DirectSteadyNavierStokes(const unsigned int degreeVelocity, const unsigned int degreePressure):
    viscosity_(1), degreeIntegration_(degreeVelocity),
    fe(FE_Q<dim>(degreeVelocity), dim, FE_Q<dim>(degreePressure), 1),
    dof_handler(triangulation),monitor(std::cout, TimerOutput::summary, TimerOutput::cpu_and_wall_times)
{}

template <class PreconditionerMp>
class BlockSchurPreconditioner : public Subscriptor
{
public:
    BlockSchurPreconditioner(double                           gamma,
                             double                           viscosity,
                             const BlockSparseMatrix<double> &S,
                             const SparseMatrix<double> &     P,
                             const PreconditionerMp &         Mppreconditioner);
    void vmult(BlockVector<double> &dst, const BlockVector<double> &src) const;
private:
    const double                     gamma;
    const double                     viscosity;
    const BlockSparseMatrix<double> &stokes_matrix;
    const SparseMatrix<double> &     pressure_mass_matrix;
    const PreconditionerMp &         mp_preconditioner;
    SparseDirectUMFPACK              A_inverse;
};

template <class PreconditionerMp>
BlockSchurPreconditioner<PreconditionerMp>::BlockSchurPreconditioner(
        double                           gamma,
        double                           viscosity,
        const BlockSparseMatrix<double> &S,
        const SparseMatrix<double> &     P,
        const PreconditionerMp &         Mppreconditioner)
        : gamma(gamma)
        , viscosity(viscosity)
        , stokes_matrix(S)
        , pressure_mass_matrix(P)
        , mp_preconditioner(Mppreconditioner)
{
    A_inverse.initialize(stokes_matrix.block(0, 0));
}

template <class PreconditionerMp>
void BlockSchurPreconditioner<PreconditionerMp>::vmult(
        BlockVector<double> &      dst,
        const BlockVector<double> &src) const
{
    Vector<double> utmp(src.block(0));
    {
        SolverControl solver_control(1000, 1e-6 * src.block(1).l2_norm());
        SolverCG<>    cg(solver_control);
        dst.block(1) = 0.0;
        cg.solve(pressure_mass_matrix,
                 dst.block(1),
                 src.block(1),
                 mp_preconditioner);
        dst.block(1) *= -(viscosity + gamma);
    }
    {
        stokes_matrix.block(0, 1).vmult(utmp, dst.block(1));
        utmp *= -1.0;
        utmp += src.block(0);
    }
    A_inverse.vmult(dst.block(0), utmp);
}

// Constructor
template <int dim>
DirectSteadyNavierStokes<dim>::~DirectSteadyNavierStokes ()
{
  triangulation.clear ();
}



template <int dim>
void DirectSteadyNavierStokes<dim>::make_cube_grid (int refinementLevel)
{
    TimerOutput::Scope timer_section(monitor, "make_cube_grid");
    const Point<dim> P1;
    const Point<dim> P2;
    if (dim==2){
        if (couette== true){

            const Point<dim> P1(-1,-1);
            const Point<dim> P2(1,1)  ;
            GridGenerator::hyper_rectangle (triangulation, P1, P2,true);
        }
        else{

            const Point<dim> P1(-1,-1);
            const Point<dim> P2(1,1)  ;
            GridGenerator::hyper_rectangle (triangulation, P1, P2,true);
        }
    }
    else if (dim==3){
        const Point<dim> P1(-1,-1,-1);
        const Point<dim> P2(2,1,1)  ;
        GridGenerator::hyper_rectangle (triangulation, P1, P2,true);
    }
    ;
  //GridGenerator::hyper_cube (triangulation, -1, 1);

  //const Point<2> center_immersed(0,0);
  //GridGenerator::hyper_ball(triangulation,center_immersed,1);
  triangulation.refine_global (5);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_grid()
{
    triangulation.refine_global(1);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::vertices_cell_mapping()
{
    //map the vertex index to the cell that include that vertex used later in which cell a point falls in
    std::cout << "vertice_to_cell:start ... "<< std::endl;
    TimerOutput::Scope timer_section(monitor, "vertice_cell_mapping");
    //vertices_to_cell is a vector of vectof of dof handler active cell iterator each element i of the vector is a vector of all the cell in contact with the vertex i
    vertices_to_cell.clear();
    vertices_to_cell.resize(dof_handler.n_dofs()/(dim+1));
    const auto &cell_iterator=dof_handler.active_cell_iterators();
    //loop on all the cell and
    for (const auto &cell : cell_iterator)  {
        unsigned int vertices_per_cell=GeometryInfo<dim>::vertices_per_cell;
        for (unsigned int i=0;i<vertices_per_cell;i++) {
            //add this cell as neighbors for all it's vertex
            unsigned int v_index = cell->vertex_index(i);
            std::vector<typename DoFHandler<dim>::active_cell_iterator> adjacent=vertices_to_cell[v_index];
            //can only add the cell if it's a set and not a vector
            std::set<typename DoFHandler<dim>::active_cell_iterator> adjacent_2(adjacent.begin(),adjacent.end());
            adjacent_2.insert(cell);
            //convert back the set to a vector and add it in the vertices_to_cell;
            std::vector<typename DoFHandler<dim>::active_cell_iterator> adjacent_3(adjacent_2.begin(),adjacent_2.end());
            vertices_to_cell[v_index]=adjacent_3;
        }
    }
    std::cout << "vertices_to_cell: done "<< std::endl;
}

template <int dim>
void DirectSteadyNavierStokes<dim>::setup_dofs ()
{
    TimerOutput::Scope timer_section(monitor, "setup_dofs");
    system_matrix.clear();
    pressure_mass_matrix.clear();
    dof_handler.distribute_dofs(fe);

    std::vector<unsigned int> block_component(dim+1, 0);
    block_component[dim] = 1;
    DoFRenumbering::component_wise (dof_handler, block_component);
    dofs_per_block.resize (2);
    DoFTools::count_dofs_per_block (dof_handler, dofs_per_block, block_component);
    unsigned int dof_u = dofs_per_block[0];
    unsigned int dof_p = dofs_per_block[1];

    FEValuesExtractors::Vector velocities(0);
    {
      nonzero_constraints.clear();
      if (couette==false) {

          DoFTools::make_hanging_node_constraints(dof_handler, nonzero_constraints);
          VectorTools::interpolate_boundary_values(dof_handler, 0, Uniform_Inlet<dim>(), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 2, Symetrics_Wall<dim>(), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 1, Symetrics_Wall<dim>(), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 3, Symetrics_Wall<dim>(), nonzero_constraints,
                                                   fe.component_mask(velocities));
          /*std::set<types::boundary_id> no_normal_flux_boundaries;
          no_normal_flux_boundaries.insert(2);
          no_normal_flux_boundaries.insert(3);
          VectorTools::compute_no_normal_flux_constraints(dof_handler,0,no_normal_flux_boundaries,nonzero_constraints);*/



          if (dim == 3) {
              VectorTools::interpolate_boundary_values(dof_handler, 4, Symetrics_Wall<dim>(), nonzero_constraints,
                                                       fe.component_mask(velocities));
              VectorTools::interpolate_boundary_values(dof_handler, 5, Symetrics_Wall<dim>(), nonzero_constraints,
                                                       fe.component_mask(velocities));
          }
      }
      else{
          DoFTools::make_hanging_node_constraints(dof_handler, nonzero_constraints);
          VectorTools::interpolate_boundary_values(dof_handler, 0, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 1, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 2, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                                   fe.component_mask(velocities));
          VectorTools::interpolate_boundary_values(dof_handler, 3, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                                   fe.component_mask(velocities));
      }
      if (simulationCase_==TaylorCouette)
      {
          VectorTools::interpolate_boundary_values(dof_handler,
                                                   1,
                                                   RotatingWall<dim>(),
                                                   nonzero_constraints,
                                                   fe.component_mask(velocities));
      }
    }
    nonzero_constraints.close();

    {
      zero_constraints.clear();
      DoFTools::make_hanging_node_constraints(dof_handler, zero_constraints);
      VectorTools::interpolate_boundary_values(dof_handler,
                                               0,
                                               ZeroFunction<dim>(dim+1),
                                               zero_constraints,
                                               fe.component_mask(velocities));
        VectorTools::interpolate_boundary_values(dof_handler,
                                                 2,
                                                 ZeroFunction<dim>(dim+1),
                                                 zero_constraints,
                                                 fe.component_mask(velocities));
        VectorTools::interpolate_boundary_values(dof_handler,
                                                 3,
                                                 ZeroFunction<dim>(dim+1),
                                                 zero_constraints,
                                                 fe.component_mask(velocities));
        VectorTools::interpolate_boundary_values(dof_handler,
                                                 1,
                                                 ZeroFunction<dim>(dim+1),
                                                 zero_constraints,
                                                 fe.component_mask(velocities));
        /*std::set<types::boundary_id> no_normal_flux_boundaries;
        no_normal_flux_boundaries.insert(2);
        no_normal_flux_boundaries.insert(3);
        VectorTools::compute_no_normal_flux_constraints(dof_handler,0,no_normal_flux_boundaries,zero_constraints);*/
        if (dim==3){
            VectorTools::interpolate_boundary_values(dof_handler,
                                                     4,
                                                     ZeroFunction<dim>(dim+1),
                                                     zero_constraints,
                                                     fe.component_mask(velocities));
            VectorTools::interpolate_boundary_values(dof_handler,
                                                     5,
                                                     ZeroFunction<dim>(dim+1),
                                                     zero_constraints,
                                                     fe.component_mask(velocities));

        }



      if (simulationCase_==TaylorCouette )
      {
          VectorTools::interpolate_boundary_values(dof_handler,
                                               1,
                                               ZeroFunction<dim>(dim+1),
                                               zero_constraints,
                                               fe.component_mask(velocities));
      }

    }
    zero_constraints.close();
    std::cout << "   Number of active cells: "
              << triangulation.n_active_cells()
              << std::endl
              << "   Number of degrees of freedom: "
              << dof_handler.n_dofs()
              << " (" << dof_u << '+' << dof_p << ')'
              << std::endl;

}

/*template <int dim>
void DirectSteadyNavierStokes<dim>::define_neighbors ()
{
    MappingQ1<dim> immersed_map;
    std::map< types::global_dof_index, Point< dim >>  	support_points;
    DoFTools::map_dofs_to_support_points(immersed_map,dof_handler,support_points);
    const auto &cell_iterator=dof_handler.active_cell_iterators();
    for (const auto &cell : cell_iterator){






    }
}*/

template <int dim>
void DirectSteadyNavierStokes<dim>::initialize_system()
{
    TimerOutput::Scope timer_section(monitor, "initialize");
  {
    BlockDynamicSparsityPattern dsp (dofs_per_block,dofs_per_block);
    DoFTools::make_sparsity_pattern (dof_handler, dsp, nonzero_constraints);
    sparsity_pattern.copy_from (dsp);
  }
  system_matrix.reinit (sparsity_pattern);
  present_solution.reinit (dofs_per_block);
  newton_update.reinit (dofs_per_block);
  system_rhs.reinit (dofs_per_block);
}

// dns
/*
template <int dim>
void DirectSteadyNavierStokes<dim>::assemble(const bool initial_step,
                                           const bool assemble_matrix)
{
  if (assemble_matrix) system_matrix    = 0;
  system_rhs       = 0;
  QGauss<dim>   quadrature_formula(degreeIntegration_+2);
  FEValues<dim> fe_values (fe,
                           quadrature_formula,
                           update_values |
                           update_quadrature_points |
                           update_JxW_values |
                           update_gradients );
  const unsigned int   dofs_per_cell = fe.dofs_per_cell;
  const unsigned int   n_q_points    = quadrature_formula.size();
  const FEValuesExtractors::Vector velocities (0);
  const FEValuesExtractors::Scalar pressure (dim);
  FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);
  Vector<double>       local_rhs    (dofs_per_cell);
  std::vector<Vector<double> >      rhs_force (n_q_points, Vector<double>(dim+1));
  std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell);
  std::vector<Tensor<1, dim> >  present_velocity_values    (n_q_points);
  std::vector<Tensor<2, dim> >  present_velocity_gradients (n_q_points);
  std::vector<double>           present_pressure_values    (n_q_points);
  std::vector<double>           div_phi_u                 (dofs_per_cell);
  std::vector<Tensor<1, dim> >  phi_u                     (dofs_per_cell);
  std::vector<Tensor<2, dim> >  grad_phi_u                (dofs_per_cell);
  std::vector<double>           phi_p                     (dofs_per_cell);
  typename DoFHandler<dim>::active_cell_iterator
  cell = dof_handler.begin_active(),
  endc = dof_handler.end();


  for (; cell!=endc; ++cell)
    {
      fe_values.reinit(cell);
      local_matrix = 0;
      local_rhs    = 0;
      fe_values[velocities].get_function_values(evaluation_point,
                                                present_velocity_values);
      fe_values[velocities].get_function_gradients(evaluation_point,
                                                   present_velocity_gradients);
      fe_values[pressure].get_function_values(evaluation_point,
                                              present_pressure_values);
      forcing_function->vector_value_list(fe_values.get_quadrature_points(),
                                                 rhs_force);

      for (unsigned int q=0; q<n_q_points; ++q)
        {
          for (unsigned int k=0; k<dofs_per_cell; ++k)
            {
              div_phi_u[k]  =  fe_values[velocities].divergence (k, q);
              grad_phi_u[k] =  fe_values[velocities].gradient(k, q);
              phi_u[k]      =  fe_values[velocities].value(k, q);
              phi_p[k]      =  fe_values[pressure]  .value(k, q);
            }
          for (unsigned int i=0; i<dofs_per_cell; ++i)
            {
              if (assemble_matrix)
                {
                  for (unsigned int j=0; j<dofs_per_cell; ++j)
                    {
                      local_matrix(i, j) += (  viscosity_*scalar_product(grad_phi_u[j], grad_phi_u[i])
                                               + present_velocity_gradients[q]*phi_u[j]*phi_u[i]
                                               + grad_phi_u[j]*present_velocity_values[q]*phi_u[i]
                                               - div_phi_u[i]*phi_p[j]
                                               - phi_p[i]*div_phi_u[j]
                                               )
                                            * fe_values.JxW(q);
                    }
                }
              const unsigned int component_i = fe.system_to_component_index(i).first;
              double present_velocity_divergence =  trace(present_velocity_gradients[q]);
              local_rhs(i) += ( - viscosity_*scalar_product(present_velocity_gradients[q],grad_phi_u[i])
                                - present_velocity_gradients[q]*present_velocity_values[q]*phi_u[i]
                                + present_pressure_values[q]*div_phi_u[i]
                                + present_velocity_divergence*phi_p[i])
                              * fe_values.JxW(q);

              local_rhs(i) += fe_values.shape_value(i,q) *
                                rhs_force[q](component_i) *
                                fe_values.JxW(q);
            }
        }


      cell->get_dof_indices (local_dof_indices);
      const AffineConstraints<double> &constraints_used = initial_step ? nonzero_constraints : zero_constraints;
      if (assemble_matrix)
        {
          constraints_used.distribute_local_to_global(local_matrix,
                                                      local_rhs,
                                                      local_dof_indices,
                                                      system_matrix,
                                                      system_rhs);
        }
      else
        {
          constraints_used.distribute_local_to_global(local_rhs,
                                                      local_dof_indices,
                                                      system_rhs);
        }
    }
}
*/
//gls


template <int dim>
void DirectSteadyNavierStokes<dim>::assemble(const bool initial_step,
                                           const bool assemble_matrix)
{
    TimerOutput::Scope timer_section(monitor, "assemble");
    if (assemble_matrix)
        system_matrix = 0;
    system_rhs = 0;
    QGauss<dim>                      quadrature_formula(degreeIntegration_ + 2);
    FEValues<dim>                    fe_values(fe,
                                               quadrature_formula,
                                               update_values | update_quadrature_points |
                                               update_JxW_values | update_gradients |
                                               update_hessians);
    const unsigned int               dofs_per_cell = fe.dofs_per_cell;
    const unsigned int               n_q_points    = quadrature_formula.size();
    const FEValuesExtractors::Vector velocities(0);
    const FEValuesExtractors::Scalar pressure(dim);
    FullMatrix<double>               local_matrix(dofs_per_cell, dofs_per_cell);
    Vector<double>                   local_rhs(dofs_per_cell);
    std::vector<Vector<double>> rhs_force(n_q_points, Vector<double>(dim + 1));
    std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
    std::vector<Tensor<1, dim>>          present_velocity_values(n_q_points);
    std::vector<Tensor<2, dim>>          present_velocity_gradients(n_q_points);
    std::vector<double>                  present_pressure_values(n_q_points);
    std::vector<Tensor<1, dim>>          present_pressure_gradients(n_q_points);
    std::vector<Tensor<1, dim>>          present_velocity_laplacians(n_q_points);
    std::vector<Tensor<2, dim>>          present_velocity_hess(n_q_points);

    Tensor<1, dim> force;


    std::vector<double>         div_phi_u(dofs_per_cell);
    std::vector<Tensor<1, dim>> phi_u(dofs_per_cell);
    std::vector<Tensor<3, dim>> hess_phi_u(dofs_per_cell);
    std::vector<Tensor<1, dim>> laplacian_phi_u(dofs_per_cell);
    std::vector<Tensor<2, dim>> grad_phi_u(dofs_per_cell);
    std::vector<double>         phi_p(dofs_per_cell);
    std::vector<Tensor<1, dim>> grad_phi_p(dofs_per_cell);



    // Element size
    double                                         h;
    typename DoFHandler<dim>::active_cell_iterator cell =
            dof_handler.begin_active(),
            endc = dof_handler.end();
    for (; cell != endc; ++cell)
    {
        fe_values.reinit(cell);
        local_matrix = 0;
        local_rhs    = 0;
        fe_values[velocities].get_function_values(evaluation_point,
                                                  present_velocity_values);
        fe_values[velocities].get_function_gradients(evaluation_point,
                                                     present_velocity_gradients);
        fe_values[pressure].get_function_values(evaluation_point,
                                                present_pressure_values);
        fe_values[pressure].get_function_gradients(evaluation_point,
                                                   present_pressure_gradients);
        fe_values[velocities].get_function_laplacians(
                evaluation_point, present_velocity_laplacians);

        forcing_function->vector_value_list(fe_values.get_quadrature_points(),
                                            rhs_force);

        if (dim == 2)
            h = std::sqrt(4. * cell->measure() / M_PI);
        else if (dim == 3)
            h = pow(6 * cell->measure() / M_PI, 1. / 3.);

        for (unsigned int q = 0; q < n_q_points; ++q)
        {
            const double u_mag =
                    std::max(present_velocity_values[q].norm(), 1e-12);
            double tau;
            tau = 1. / std::sqrt(std::pow(2. * u_mag / h, 2) +
                                 9 * std::pow(4 * viscosity_ / (h * h), 2));

            for (unsigned int k = 0; k < dofs_per_cell; ++k)
            {
                div_phi_u[k]  = fe_values[velocities].divergence(k, q);
                grad_phi_u[k] = fe_values[velocities].gradient(k, q);
                phi_u[k]      = fe_values[velocities].value(k, q);
                hess_phi_u[k] = fe_values[velocities].hessian(k, q);
                phi_p[k]      = fe_values[pressure].value(k, q);
                grad_phi_p[k] = fe_values[pressure].gradient(k, q);

                for (int d = 0; d < dim; ++d)
                    laplacian_phi_u[k][d] = trace(hess_phi_u[k][d]);
            }

            // Establish the force vector
            for (int i = 0; i < dim; ++i)
            {
                const unsigned int component_i =
                        this->fe.system_to_component_index(i).first;
                force[i] = rhs_force[q](component_i);
            }

            auto strong_residual =
                    present_velocity_gradients[q] * present_velocity_values[q] +
                    present_pressure_gradients[q] -
                    viscosity_ * present_velocity_laplacians[q] - force;

            if (assemble_matrix)
            {
                for (unsigned int j = 0; j < dofs_per_cell; ++j)
                {
                    auto strong_jac =
                            (present_velocity_gradients[q] * phi_u[j] +
                             grad_phi_u[j] * present_velocity_values[q] +
                             grad_phi_p[j] - viscosity_ * laplacian_phi_u[j]);

                    for (unsigned int i = 0; i < dofs_per_cell; ++i)
                    {
                        local_matrix(i, j) +=
                                (viscosity_ *
                                 scalar_product(grad_phi_u[j], grad_phi_u[i]) +
                                 present_velocity_gradients[q] * phi_u[j] * phi_u[i] +
                                 grad_phi_u[j] * present_velocity_values[q] * phi_u[i] -
                                 div_phi_u[i] * phi_p[j] + phi_p[i] * div_phi_u[j]) *
                                fe_values.JxW(q);


                        // PSPG GLS term
                        local_matrix(i, j) +=
                                tau * strong_jac * grad_phi_p[i] * fe_values.JxW(q);


                        // PSPG TAU term is currently disabled because it does
                        // not alter the matrix sufficiently
                        // local_matrix(i, j) +=
                        //  -tau * tau * tau * 4 / h / h *
                        //  (present_velocity_values[q] * phi_u[j]) *
                        //  strong_residual * grad_phi_p[i] *
                        //  fe_values.JxW(q);

                        // Jacobian is currently incomplete
                        if (true)
                        {
                            local_matrix(i, j) +=
                                    tau *
                                    (strong_jac *
                                     (grad_phi_u[i] * present_velocity_values[q]) +
                                     strong_residual * (grad_phi_u[i] * phi_u[j])) *
                                    fe_values.JxW(q);

                            // SUPG TAU term is currently disabled because it
                            // does not alter the matrix sufficiently
                            // local_matrix(i, j)
                            // +=
                            //   -strong_residual
                            //   * (grad_phi_u[i]
                            //   *
                            //   present_velocity_values[q])
                            //   * tau * tau *
                            //   tau * 4 / h / h
                            //   *
                            //   (present_velocity_values[q]
                            //   * phi_u[j]) *
                            //   fe_values.JxW(q);
                        }
                    }
                }
            }
            for (unsigned int i = 0; i < dofs_per_cell; ++i)
            {
                double present_velocity_divergence =
                        trace(present_velocity_gradients[q]);
                local_rhs(i) +=
                        (-viscosity_ * scalar_product(present_velocity_gradients[q],
                                                      grad_phi_u[i]) -
                         present_velocity_gradients[q] * present_velocity_values[q] *
                         phi_u[i] +
                         present_pressure_values[q] * div_phi_u[i] -
                         present_velocity_divergence * phi_p[i] + force * phi_u[i]) *
                        fe_values.JxW(q);

                // PSPG GLS term
                local_rhs(i) +=
                        -tau * (strong_residual * grad_phi_p[i]) * fe_values.JxW(q);

                // SUPG GLS term
                if (true)
                {
                    local_rhs(i) +=
                            -tau *
                            (strong_residual *
                             (grad_phi_u[i] * present_velocity_values[q])) *
                            fe_values.JxW(q);
                }
            }
        }


        cell->get_dof_indices(local_dof_indices);
        const AffineConstraints<double> &constraints_used =
                initial_step ? nonzero_constraints : zero_constraints;
        if (assemble_matrix)
        {
            constraints_used.distribute_local_to_global(local_matrix,
                                                        local_rhs,
                                                        local_dof_indices,
                                                        system_matrix,
                                                        system_rhs);
        }
        else
        {
            constraints_used.distribute_local_to_global(local_rhs,
                                                        local_dof_indices,
                                                        system_rhs);
        }
    }
    //if (assemble_matrix)
    //{
        // Finally we move pressure mass matrix into a separate matrix:
        //pressure_mass_matrix.reinit(sparsity_pattern.block(1, 1));
        //pressure_mass_matrix.copy_from(system_matrix.block(1, 1));

        // Note that settings this pressure block to zero is not identical to
        // not assembling anything in this block, because this operation here
        // will (incorrectly) delete diagonal entries that come in from
        // hanging node constraints for pressure DoFs. This means that our
        // whole system matrix will have rows that are completely
        // zero. Luckily, FGMRES handles these rows without any problem.
        //system_matrix.block(1, 1) = 0;
    //}
}



template <int dim>
void DirectSteadyNavierStokes<dim>::assemble_system(const bool initial_step)
{
  assemble(initial_step, true);
}
template <int dim>
void DirectSteadyNavierStokes<dim>::assemble_rhs(const bool initial_step)
{
  assemble(initial_step, false);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::solve (const bool initial_step)
{
    TimerOutput::Scope timer_section(monitor, "solve");
  const AffineConstraints<double> &constraints_used = initial_step ? nonzero_constraints : zero_constraints;
  SparseDirectUMFPACK direct;
  direct.initialize(system_matrix);
  direct.vmult(newton_update,system_rhs);
  constraints_used.distribute(newton_update);

  /*SolverControl solver_control(10000, 1e-12,true,true);
  solver_control.log_frequency(1);
  SolverGMRES<>    solver(solver_control);
  SparseILU<double> preconditioner;

  //SparseILU<double> preconditioner;
  preconditioner.initialize(system_matrix);
  solver.solve(system_matrix, newton_update, system_rhs,preconditioner);
  constraints_used.distribute(newton_update);*/
}

template <int dim>
void DirectSteadyNavierStokes<dim>::solve_2(const bool initial_step)
{
    const AffineConstraints<double> &constraints_used =
            initial_step ? nonzero_constraints : zero_constraints;
    SolverControl solver_control(system_matrix.m(),
                                 1e-4 * system_rhs.l2_norm(),
                                 true);
    SolverFGMRES<BlockVector<double>> gmres(solver_control);
    SparseILU<double>                 pmass_preconditioner;
    pmass_preconditioner.initialize(pressure_mass_matrix,SparseILU<double>::AdditionalData());
    const BlockSchurPreconditioner<SparseILU<double>> preconditioner(
            1,
            viscosity_,
            system_matrix,
            pressure_mass_matrix,
            pmass_preconditioner);
    gmres.solve(system_matrix, newton_update, system_rhs, preconditioner);
    std::cout << "FGMRES steps: " << solver_control.last_step() << std::endl;
    constraints_used.distribute(newton_update);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_mesh ()
{
    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());
    FEValuesExtractors::Vector velocity(0);
    KellyErrorEstimator<dim>::estimate (dof_handler,
                                        QGauss<dim-1>(degreeIntegration_+1),
                                        typename std::map<types::boundary_id, const Function<dim, double> *>(),
                                        present_solution,
                                        estimated_error_per_cell,
                                        fe.component_mask(velocity));
    GridRefinement::refine_and_coarsen_fixed_number (triangulation,
                                                     estimated_error_per_cell,
                                                     1, 0.0);
    triangulation.prepare_coarsening_and_refinement();
    SolutionTransfer<dim, BlockVector<double> > solution_transfer(dof_handler);
    solution_transfer.prepare_for_coarsening_and_refinement(present_solution);
    triangulation.execute_coarsening_and_refinement ();
    setup_dofs();
    BlockVector<double> tmp (dofs_per_block);
    solution_transfer.interpolate(present_solution, tmp);
    nonzero_constraints.distribute(tmp);
    initialize_system();
    present_solution = tmp;
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_mesh_uniform ()
{
    SolutionTransfer<dim, BlockVector<double> > solution_transfer(dof_handler);
    solution_transfer.prepare_for_coarsening_and_refinement(present_solution);
    triangulation.refine_global(1);
    setup_dofs();
    BlockVector<double> tmp (dofs_per_block);
    solution_transfer.interpolate(present_solution, tmp);
    nonzero_constraints.distribute(tmp);
    initialize_system();
    present_solution = tmp;
}

template <int dim>
void DirectSteadyNavierStokes<dim>::sharp_edge_V2(const bool initial_step) {
    TimerOutput::Scope timer_section(monitor, "sharp_edge");
    //This function define a immersed boundary base on the sharp edge method on a hyper_shere of dim 2 or 3


    //define stuff  in a later version the center of the hyper_sphere would be defined by a particule handler and the boundary condition associeted with it also.
    using numbers::PI;
    const double center_x=0.2;
    const double center_y=0;
    const Point<dim> center_immersed(center_x,center_y);
    if (dim==2)
        const Point<dim> center_immersed(center_x,center_y);
    else if (dim==3)
        const Point<dim> center_immersed(center_x,center_y,center_x);

    std::vector<typename DoFHandler<dim>::active_cell_iterator> active_neighbors;
    std::cout << "center immersed" << center_immersed << std::endl;

    //define a map to all dof and it's support point
    MappingQ1<dim> immersed_map;
    std::map< types::global_dof_index, Point< dim >>  	support_points;
    DoFTools::map_dofs_to_support_points(immersed_map,dof_handler,support_points);

    // initalise fe value object in order to do calculation with it later
    QGauss<dim> q_formula(fe.degree+1);
    FEValues<dim> fe_values(fe, q_formula,update_quadrature_points);
    const unsigned int dofs_per_cell = fe.dofs_per_cell;

    // define multiple local_dof_indices one for the cell iterator one for the cell with the second point for
    // the sharp edge stancil and one for manipulation on the neighbors cell.
    std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_2(dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_3(dofs_per_cell);



    //define cell ieteator
    const auto &cell_iterator=dof_handler.active_cell_iterators();

    //define the minimal cell side length
    //double min_cell_d=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    //std::cout << "min cell dist: " << min_cell_d << std::endl;

    //loop on all the cell to define if the sharp edge cut them
    for (const auto &cell : cell_iterator)  {
        fe_values.reinit(cell);
        cell->get_dof_indices(local_dof_indices);
        unsigned int count_small=0;
        unsigned int count_large=0;
        for (unsigned int j = 0; j < local_dof_indices.size(); ++j) {
            //count the number of dof that ar smaller or larger then the radius of the particules
            //if all the dof are on one side the cell is not cut by the boundary meaning we dont have to do anything
            if ((support_points[local_dof_indices[j]]-center_immersed).norm()<=radius){
                ++count_small;
            }
            if ((support_points[local_dof_indices[j]]-center_immersed).norm()<=radius_2){
                ++count_large;
            }
        }

        if (couette==false){
        count_large=0;
        }
        //if the cell is cut by the IB the count wont equal 0 or the number of total dof in a cell
        if (count_small!=0 and count_small!=local_dof_indices.size()){
            //if we are here the cell is cut by the immersed boundary

            //loops on the dof that reprensant the velocity   in x and y and pressure separatly
            for (unsigned int k = 0; k< dim+1; ++k) {

                if (k < dim) {
                    //we are working on the velocity of th
                    unsigned int l = k;

                    //loops on the dof that are for vx or vy separatly
                    while (l < local_dof_indices.size()) {
                        //define the distance vector between the immersed boundary and the dof support point for each dof
                        Tensor<1, dim, double> vect_dist = (support_points[local_dof_indices[l]]-center_immersed - radius *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());
                        std::cout << "vect_dist: " << vect_dist  << std::endl;
                        //define the other point for or 3 point stencil ( IB point, original dof and this point)
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + vect_dist);
                        //define the vertex associated with the dof
                        unsigned int v=floor(l/(dim+1));
                        unsigned int v_index= cell->vertex_index(v);

                        //get a cell iterator for all the cell neighbors of that vertex
                        active_neighbors=vertices_to_cell[v_index];

                        unsigned int cell_found=0;
                        unsigned int n_active_cells= active_neighbors.size();

                        //loops on those cell to find in which of them the new point for or sharp edge stencil is
                        for (unsigned int cell_index = 0; cell_index < n_active_cells; ++cell_index) {
                            try {
                                //define the cell and check if the point is inside of the cell
                                const auto &cell_3 = active_neighbors[cell_index];
                                const Point<dim> p_cell = immersed_map.transform_real_to_unit_cell(
                                        active_neighbors[cell_index], second_point);
                                const double dist_2 = GeometryInfo<dim>::distance_to_unit_cell(p_cell);
                                //define the cell and check if the point is inside of the cell

                                if (dist_2 == 0) {
                                    //if the point is in this cell then the dist is equal to 0 and we have found our cell
                                    cell_found = cell_index;
                                    break;
                                }
                            }
                            // may cause error if the point is not in cell
                            catch (typename MappingQGeneric<dim>::ExcTransformationFailed) {
                            }
                        }

                        //we have or next cell need to complet the stencil and we define stuff around it
                        const auto &cell_2 = active_neighbors[cell_found];
                        //define the unit cell point for the 3rd point of our stencil for a interpolation
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);
                        // define which dof is going to be redefine
                        unsigned int global_index_overrigth = local_dof_indices[l];
                            //get a idea of the order of magnetude of the value in the matrix to define the stencil in the same range of value
                        double sum_line = system_matrix(global_index_overrigth, global_index_overrigth);


                        //clear the current line of this dof  by looping on the neighbors cell of this dof and clear all the associated dof
                        for (unsigned int m = 0; m < active_neighbors.size(); m++) {
                            const auto &cell_3 = active_neighbors[m];
                            cell_3->get_dof_indices(local_dof_indices_3);
                            for (unsigned int o = 0; o < local_dof_indices_2.size(); ++o) {
                                system_matrix.set(global_index_overrigth, local_dof_indices_3[o], 0);
                            }
                        }

                        //define the new matrix entry for this dof
                        if(vect_dist.norm()!=0) {
                            // first the dof itself
                            system_matrix.set(global_index_overrigth, global_index_overrigth,
                                              -2 / (1 / sum_line));

                            unsigned int n = k;
                            // then the third point trough interpolation from the dof of the cell in which the third point is
                            while (n < local_dof_indices_2.size()) {
                                system_matrix.add(global_index_overrigth, local_dof_indices_2[n],
                                                  fe.shape_value(n, second_point_v) / (1 / sum_line));

                                if (n < (dim + 1) * 4) {
                                    n = n + dim + 1;
                                } else {
                                    n = n + dim;
                                }
                            }
                        }
                        else{
                            system_matrix.set(global_index_overrigth, global_index_overrigth,
                                                sum_line);

                        }
                        // define our second point and last to be define the immersed boundary one  this point is where we applied the boundary conmdition as a dirichlet
                        if (couette==true & initial_step)
                        {
                            // different boundary condition depending if the dof is vx or vy and of the problem we solve
                        if (k == 0) {
                            system_rhs(global_index_overrigth) = 1 * ((support_points[local_dof_indices[l]] -
                                                                       center_immersed) /
                                                                      (support_points[local_dof_indices[l]] -
                                                                       center_immersed).norm())[1] / (1/sum_line);
                        }
                        else {
                            system_rhs(global_index_overrigth) = -1 * ((support_points[local_dof_indices[l]] -
                                                                        center_immersed) /
                                                                       (support_points[local_dof_indices[l]] -
                                                                        center_immersed).norm())[0] / (1/sum_line);
                        }
                        }

                        else{
                            system_rhs(global_index_overrigth) =0;
                        }
                        if (couette==false )
                            system_rhs(global_index_overrigth)=0;


                        // add index ( this is for P2 element when dof are not at the nodes but should be replace because rest of the code those not workj with that for the moment
                        if (l < (dim + 1) * 4) {
                            l = l + dim + 1;
                        } else {
                            l = l + dim;
                        }
                    }

                }
                }
            }
        // same as previous but for large circle in the case of couette flow
        if (count_large!=0 and count_large!=local_dof_indices.size()){
            //cellule coupe par boundary large
            for (unsigned int k = 0; k< dim+1; ++k) {
                if (k < 2) {
                    unsigned int l = k;
                    while (l < local_dof_indices.size()) {
                        Tensor<1, dim, double> vect_dist = (support_points[local_dof_indices[l]] - radius_2 *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());
                        double dist = vect_dist.norm();
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + vect_dist);
                        unsigned int v=floor(l/(dim+1));

                        unsigned int v_index= cell->vertex_index(v);
                        //active_neighbors=GridTools::find_cells_adjacent_to_vertex(dof_handler,v_index);
                        active_neighbors=vertices_to_cell[v_index];
                        unsigned int cell_found=0;
                        unsigned int n_active_cells= active_neighbors.size();

                        for (unsigned int cell_index=0;  cell_index < n_active_cells;++cell_index){
                            try {
                                const auto &cell_3=active_neighbors[cell_index];
                                const Point<dim> p_cell = immersed_map.transform_real_to_unit_cell(active_neighbors[cell_index], second_point);
                                const double dist = GeometryInfo<dim>::distance_to_unit_cell(p_cell);
                                if (dist == 0) {
                                    cell_found=cell_index;
                                    break;
                                }
                            }
                            catch (typename MappingQGeneric<dim>::ExcTransformationFailed ){
                            }
                        }


                        const auto &cell_2 = active_neighbors[cell_found];
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);

                        unsigned int global_index_overrigth = local_dof_indices[l];
                        double sum_line=system_matrix(global_index_overrigth, global_index_overrigth);

                            for (unsigned int m = 0; m < active_neighbors.size(); m++) {
                                const auto &cell_3 = active_neighbors[m];
                                cell_3->get_dof_indices(local_dof_indices_3);
                                for (unsigned int o = 0; o < local_dof_indices_2.size(); ++o) {
                                    //sum_line += system_matrix(global_index_overrigth, local_dof_indices_3[o]);
                                    system_matrix.set(global_index_overrigth, local_dof_indices_3[o], 0);
                                }
                            }

                            system_matrix.set(global_index_overrigth, global_index_overrigth,
                                              -2 / (1 / sum_line));
                            unsigned int n = k;
                            while (n < local_dof_indices_2.size()) {
                                system_matrix.add(global_index_overrigth, local_dof_indices_2[n],
                                                  fe.shape_value(n, second_point_v) / (1 / sum_line));
                                if (n < (dim + 1) * 4) {
                                    n = n + dim + 1;
                                } else {
                                    n = n + dim;
                                }
                            }
                        system_rhs(global_index_overrigth) = 0;
                        /*if (couette==false) {
                            if (k == 0) {
                                system_rhs(global_index_overrigth) = 0;
                            } else {
                                system_rhs(global_index_overrigth) = 0;
                            }
                        }*/

                        if (l < (dim + 1) * 4) {
                            l = l + dim + 1;
                        } else {
                            l = l + dim;
                        }
                    }

                }
            }
        }
    }
}

template<int dim>
void DirectSteadyNavierStokes<dim>::torque()
{
    // cumpute the torque for a couet flow on the immersed boundary
    using numbers::PI;
    const double center_x=0;
    const double center_y=0;


    QGauss<dim> q_formula(fe.degree+1);
    FEValues<dim> fe_values(fe, q_formula,update_quadrature_points);

    const Point<2> center_immersed(center_x,center_y);
    double mu=viscosity_;

    MappingQ1<dim> immersed_map;
    std::vector<types::global_dof_index> local_dof_indices(fe.dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_2(fe.dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_3(fe.dofs_per_cell);
    unsigned int nb_evaluation=100;
    double t_torque=0;
    double t_torque_l=0;
    double fx_v=0;
    double fy_v=0;

    double fx_p=0;
    double fy_p=0;
    double fx_p_0=0;
    double fy_p_0=0;
    double fx_p_1=0;
    double fy_p_1=0;
    double fx_p_2=0;
    double fy_p_2=0;

    double T_in=0;
    double dr=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    dr=dr*2;
    for (unsigned int i=0;i<nb_evaluation;++i ) {
        const Point<dim> eval_point(radius * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius * sin(i * 2 * PI / (nb_evaluation)) + center_y);
        const auto &cell = GridTools::find_active_cell_around_point(dof_handler, eval_point);
        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell, eval_point);
        cell->get_dof_indices(local_dof_indices);
        double u_1=0;
        double v_1=0;
        u_1=-sin(i * 2 * PI / (nb_evaluation));
        v_1=cos(i * 2 * PI / (nb_evaluation));
        double U1=u_1*cos(i * 2 * PI / (nb_evaluation)-PI/2)+v_1*sin(i * 2 * PI / (nb_evaluation)-PI/2);

        const Point<dim> eval_point_2(eval_point[0]+dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]+dr*sin(i * 2 * PI / (nb_evaluation)));
        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, eval_point_2);
        second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, eval_point_2);
        cell_2->get_dof_indices(local_dof_indices);
        double u_2=0;
        double v_2=0;
        for (unsigned int j=0;j<12;j=j+3 ){
            u_2+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
            v_2+=fe.shape_value(j+1,second_point_v)*present_solution(local_dof_indices[j+1]);
        }
        double U2=u_2*cos(i * 2 * PI / (nb_evaluation)-PI/2)+v_2*sin(i * 2 * PI / (nb_evaluation)-PI/2);
        double du_dr=(U2/(radius+dr)-U1/radius)/dr;
        //std::cout << "du_dr " <<du_dr << std::endl;
        //std::cout << "local shear stress: " <<du_dr*mu*radius << std::endl;
        t_torque+=radius*du_dr*mu*radius*2*PI*radius/(nb_evaluation-1) ;
        fx_v+=du_dr*mu*radius*2*PI*radius/(nb_evaluation-1)*cos(i * 2 * PI / (nb_evaluation)-PI/2);
        fy_v+=du_dr*mu*radius*2*PI*radius/(nb_evaluation-1)*sin(i * 2 * PI / (nb_evaluation)-PI/2);
    }

    std::cout << "total_torque_small " << t_torque << std::endl;

    for (unsigned int i=0;i<nb_evaluation;++i ) {
        const Point<dim> eval_point(radius_2 * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius_2 * sin(i * 2 * PI / (nb_evaluation)) + center_y);
        const auto &cell = GridTools::find_active_cell_around_point(dof_handler, eval_point);
        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell, eval_point);
        cell->get_dof_indices(local_dof_indices);
        double u_1=0;
        double v_1=0;
        /*for (unsigned int j=0;j<12;j=j+3 ){
            u_1+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
            v_1+=fe.shape_value(j+1,second_point_v)*present_solution(local_dof_indices[j+1]);
        }*/
        u_1=0;
        v_1=0;
        double U1=u_1*cos(i * 2 * PI / (nb_evaluation)-PI/2)+v_1*sin(i * 2 * PI / (nb_evaluation)-PI/2);
        const Point<dim> eval_point_2(eval_point[0]-dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]-dr*sin(i * 2 * PI / (nb_evaluation)));
        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, eval_point_2);
        second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, eval_point_2);
        cell_2->get_dof_indices(local_dof_indices);
        double u_2=0;
        double v_2=0;
        for (unsigned int j=0;j<12;j=j+3 ){
            u_2+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
            v_2+=fe.shape_value(j+1,second_point_v)*present_solution(local_dof_indices[j+1]);
        }
        double U2=u_2*cos(i * 2 * PI / (nb_evaluation)-PI/2)+v_2*sin(i * 2 * PI / (nb_evaluation)-PI/2);
        double du_dr=(U2/(radius_2-dr)-U1/radius_2)/dr;

        //std::cout << "local shear stress: " <<radius_2*du_dr*mu << std::endl;
        t_torque_l+=radius_2*du_dr*mu*radius_2*2*PI*radius_2/(nb_evaluation-1) ;

    }
    std::cout << "total_torque_large" << t_torque_l << std::endl;

    //pressure force evaluation
    for (unsigned int i=0;i<nb_evaluation;++i ) {
        const Point<dim> eval_point(radius * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius * sin(i * 2 * PI / (nb_evaluation)) + center_y);
        const auto &cell = GridTools::find_active_cell_around_point(dof_handler, eval_point);
        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell, eval_point);
        cell->get_dof_indices(local_dof_indices);
        double P=0;
        for (unsigned int j=2;j<12;j=j+3 ){
            P+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
        }

        fx_p+=P*-cos(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fy_p+=P*-sin(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;

    }
    std::cout << "fx_P: " << fx_p << std::endl;
    std::cout << "fy_P: " << fy_p << std::endl;
    fx_p_0=0;
    fy_p_0=0;
    fx_p_1=0;
    fy_p_1=0;
    fx_p_2=0;
    fy_p_2=0;
    for (unsigned int i=0;i<nb_evaluation;++i ) {

        const Point<dim> eval_point(radius * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius * sin(i * 2 * PI / (nb_evaluation)) + center_y);
        const Point<dim> eval_point_2(eval_point[0]+1*dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]+1*dr*sin(i * 2 * PI / (nb_evaluation)));
        const Point<dim> eval_point_3(eval_point[0]+2*dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]+2*dr*sin(i * 2 * PI / (nb_evaluation)));
        const Point<dim> eval_point_4(eval_point[0]+3*dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]+3*dr*sin(i * 2 * PI / (nb_evaluation)));
        const auto &cell = GridTools::find_active_cell_around_point(dof_handler, eval_point_2);
        const auto &cell2 = GridTools::find_active_cell_around_point(dof_handler, eval_point_3);
        const auto &cell3 = GridTools::find_active_cell_around_point(dof_handler, eval_point_4);

        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell, eval_point_2);
        Point<dim> second_point_v_2 = immersed_map.transform_real_to_unit_cell(cell2, eval_point_3);
        Point<dim> second_point_v_3 = immersed_map.transform_real_to_unit_cell(cell3, eval_point_4);
        cell->get_dof_indices(local_dof_indices);
        cell2->get_dof_indices(local_dof_indices_2);
        cell3->get_dof_indices(local_dof_indices_3);
        double P_1=0;
        double P_2=0;
        double P_3=0;
        for (unsigned int j=2;j<12;j=j+3 ){
            P_1+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
            P_2+=fe.shape_value(j,second_point_v_2)*present_solution(local_dof_indices_2[j]);
            P_3+=fe.shape_value(j,second_point_v_3)*present_solution(local_dof_indices_3[j]);
        }
        double P2_temp=P_1+(P_1-P_2)+((P_1-P_2)-(P_2-P_3));
        double P2=P2_temp;//+(P2_temp-P_1)+((P2_temp-P_1)-(P_1-P_2));
        double P=P_1+(P_1-P_2)*1;
        double P3=P_1;
        fx_p_2+=P2*-cos(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fy_p_2+=P2*-sin(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fx_p_1+=P*-cos(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fy_p_1+=P*-sin(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fx_p_0+=P3*-cos(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;
        fy_p_0+=P3*-sin(i * 2 * PI / (nb_evaluation))*2*PI*radius/(nb_evaluation-1) ;

    }
    std::cout << "ordre 0 fx_P: " << fx_p_0 << std::endl;
    std::cout << "ordre 0 fy_P: " << fy_p_0 << std::endl;
    std::cout << "ordre 1 fx_P: " << fx_p_1 << std::endl;
    std::cout << "ordre 1 fy_P: " << fy_p_1 << std::endl;
    std::cout << "ordre 2 fx_P: " << fx_p_2 << std::endl;
    std::cout << "ordre 2 fy_P: " << fy_p_2 << std::endl;
    std::cout << "fx_v: " << fx_v << std::endl;
    std::cout << "fy_v: " << fy_v << std::endl;
}

template <int dim>
void DirectSteadyNavierStokes<dim>::newton_iteration(const double tolerance,
                                                   const unsigned int max_iteration,
                                                   const bool  is_initial_step,
                                                   const bool  /*output_result*/)
{
  double current_res;
  double last_res;
  bool   first_step = is_initial_step;
    {
      unsigned int outer_iteration = 0;
      last_res = 1.0;
      current_res = 1.0;
      while ((first_step || (current_res > tolerance)) && outer_iteration < max_iteration)
        {
          if (first_step)
            {
              initialize_system();
              evaluation_point = present_solution;
              assemble_system(first_step);
              vertices_cell_mapping();
              sharp_edge_V2(first_step);
              current_res = system_rhs.l2_norm();
              //std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res << std::endl;
              solve(first_step);
              present_solution = newton_update;
              nonzero_constraints.distribute(present_solution);
              first_step = false;
              evaluation_point = present_solution;
              assemble_rhs(first_step);
              current_res = system_rhs.l2_norm();
              last_res = current_res;
              last_vect.reinit(present_solution);
              last_vect=present_solution;
            }
          else
            {
              std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res << std::endl;
              evaluation_point = present_solution;
              assemble_system(first_step);
              sharp_edge_V2(first_step);
              current_res = system_rhs.l2_norm();
              solve(first_step);
              for (double alpha = 1.0; alpha > 1e-3; alpha *= 0.5)
                {
                  evaluation_point = present_solution;
                  evaluation_point.add(alpha, newton_update);
                  nonzero_constraints.distribute(evaluation_point);
                  //current_res = newton_update.l2_norm();

                  std::cout  <<  "  - Residual:  " << current_res << std::endl;

                  assemble_rhs(first_step);
                  sharp_edge_V2(first_step);
                  //std::cout  <<  "  - Residual 2:  " << system_rhs.linfty_norm() << std::endl;

                  present_solution = evaluation_point;
                  current_res = system_rhs.l2_norm();
                  last_vect-= present_solution;
                  //current_res = system_rhs.l2_norm();
                  //current_res = last_vect.l2_norm();

                  last_vect.reinit(present_solution);
                  last_vect=present_solution;
                  //std::cout << "residual : "<<  current_res << std::endl;

                    std::cout << "\t\talpha = " << std::setw(6) << alpha
                              << std::setw(0) << " res = " << current_res
                              << std::endl;
                    if (current_res < last_res)
                        break;
                    last_res = current_res;
              }
            }
          ++outer_iteration;

        }
    }
}

template <int dim>
void DirectSteadyNavierStokes<dim>::output_results (const unsigned int cycle) const
{
    std::vector<std::string> solution_names (dim, "velocity");
    solution_names.push_back ("pressure");

    std::vector<DataComponentInterpretation::DataComponentInterpretation>
    data_component_interpretation
    (dim, DataComponentInterpretation::component_is_part_of_vector);
    data_component_interpretation
    .push_back (DataComponentInterpretation::component_is_scalar);

    DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (present_solution, solution_names, DataOut<dim>::type_dof_data, data_component_interpretation);
    data_out.build_patches (1);

    std::string filenamesolution = "solution-";
    filenamesolution += ('0' + cycle);
    filenamesolution += ".vtk";

    std::cout << "Writing file : " << filenamesolution << std::endl;
    std::ofstream outputSolution (filenamesolution.c_str());

    data_out.write_vtk (outputSolution);
}

//Find the l2 norm of the error between the finite element sol'n and the exact sol'n
template <int dim>
void DirectSteadyNavierStokes<dim>::calculateL2Error()
{

    QGauss<dim>  quadrature_formula(fe.degree+2);
    FEValues<dim> fe_values (fe, quadrature_formula,
                             update_values   | update_gradients |
                             update_quadrature_points | update_JxW_values);

    const FEValuesExtractors::Vector velocities (0);
    const FEValuesExtractors::Scalar pressure (dim);

    MappingQ1<dim> immersed_map;
    std::map< types::global_dof_index, Point< dim >>  	support_points;
    DoFTools::map_dofs_to_support_points(immersed_map,dof_handler,support_points);

    const unsigned int   			dofs_per_cell = fe.dofs_per_cell;         // This gives you dofs per cell
    std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell); //  Local connectivity

    const unsigned int   n_q_points    = quadrature_formula.size();
    double l2errorU=0.;
    double l2errorU_2=0.;
    double l2errorU_3=0.;
    double l2errorU_4=0.;
    double min_cell_d=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    std::vector<Vector<double> > q_exactSol (n_q_points, Vector<double>(dim+1));
    std::vector<Vector<double> > q_exactSol_2 (4, Vector<double>(dim+1));


    std::vector<Tensor<1,dim> > local_velocity_values (n_q_points);
    std::vector<double > local_pressure_values (n_q_points);

    double maxPressure=-DBL_MAX;
    // Get the maximal value of the pressure
    for (auto icell=dof_handler.begin_active(); icell!=dof_handler.end(); ++icell)
    {
        fe_values.reinit (icell);
        fe_values[pressure].get_function_values(present_solution,
                                                local_pressure_values);

        for (unsigned int i=0 ; i<local_pressure_values.size() ; ++i)
        {
            maxPressure=std::max(local_pressure_values[i],maxPressure);
        }

    }

    //loop over elements
    typename DoFHandler<dim>::active_cell_iterator
            cell = dof_handler.begin_active(),
            endc = dof_handler.end();
    for (; cell!=endc; ++cell) {
        fe_values.reinit(cell);
        fe_values[velocities].get_function_values(present_solution,
                                                  local_velocity_values);
        fe_values[pressure].get_function_values(present_solution,
                                                local_pressure_values);

        //Retrieve the effective "connectivity matrix" for this element
        cell->get_dof_indices(local_dof_indices);

        // Get the exact solution at all gauss points
        exact_solution->vector_value_list(fe_values.get_quadrature_points(),
                                          q_exactSol);

        for (unsigned int q = 0; q < n_q_points; q++) {
            //Find the values of x and u_h (the finite element solution) at the quadrature points

            //std::cout << "quadrature point location : " << fe_values.get_quadrature_points()[q]<< std::endl;
            double ux_sim = local_velocity_values[q][0];
            double ux_exact = q_exactSol[q][0];

            double uy_sim = local_velocity_values[q][1];
            double uy_exact = q_exactSol[q][1];

            l2errorU += (ux_sim - ux_exact) * (ux_sim - ux_exact) * fe_values.JxW(q);
            l2errorU += (uy_sim - uy_exact) * (uy_sim - uy_exact) * fe_values.JxW(q);
            if (fe_values.get_quadrature_points()[q].norm() < radius_2-min_cell_d &
                fe_values.get_quadrature_points()[q].norm() > radius+min_cell_d) {
                l2errorU_2 += (ux_sim - ux_exact) * (ux_sim - ux_exact) * fe_values.JxW(q);
                l2errorU_2 += (uy_sim - uy_exact) * (uy_sim - uy_exact) * fe_values.JxW(q);
            }
            // std::cout << "local Error is : " << (uy_sim-uy_exact) << std::endl;
            //std::cout << "local exact solution is :" << (uy_exact) << std::endl;
            //std::cout << "local  solution is :" << (uy_sim) << std::endl;
        }
    }
    int count_1=0;
    for(unsigned int i=0; i<dof_handler.n_dofs()/(dim+1); i=i+1){
            //Find the values of x and u_h (the finite element solution) at the quadrature points

            //std::cout << "quadrature point location : " << fe_values.get_quadrature_points()[q]<< std::endl;
        const Point<dim> P= support_points[dim*i];
        //std::cout << "point :" << P << std::endl;


        double r= P.norm();

        double theta= std::atan2(P[1],P[0]);
        double omega_1=1/radius;
        double omega_2=0;
        double ri_=radius;
        double ro_=radius_2;

        double A= (omega_2*ro_*ro_-omega_1*ri_*ri_)/(ro_*ro_-ri_*ri_);
        double B= (omega_1-omega_2)*ri_*ri_*ro_*ro_/(ro_*ro_-ri_*ri_);
        double utheta= A*r + B/r;
        if (r>ro_)
            utheta=0;
        if (r<ri_)
            utheta=omega_1*r;


        double ux_exact = -std::sin(theta)*utheta;
        double uy_exact = std::cos(theta)*utheta;

        double ux_sim=present_solution[i*dim];
        double uy_sim=present_solution[i*dim+1];


        l2errorU_3 += (ux_sim-ux_exact)*(ux_sim-ux_exact) ;
        l2errorU_3 += (uy_sim-uy_exact)*(uy_sim-uy_exact) ;
        if ( r<radius_2-min_cell_d & r>radius+min_cell_d)
        {

            l2errorU_4 += (ux_sim-ux_exact)*(ux_sim-ux_exact) ;
            l2errorU_4 += (uy_sim-uy_exact)*(uy_sim-uy_exact) ;
            count_1+=2;
        }
            // std::cout << "local Error is : " << (uy_sim-uy_exact) << std::endl;
        //std::cout << "local exact solution is :" << (uy_exact) << std::endl;
        //std::cout << "local  solution is :" << (uy_sim) << std::endl;
    }

    std::cout << "L2Error global is : " << std::sqrt(l2errorU) << std::endl;
    std::cout << "L2Error between the 2 cylinder is : " << std::sqrt(l2errorU_2) << std::endl;
    std::cout << "L2Error global is : " << std::sqrt(l2errorU_3/(dof_handler.n_dofs()*dim/(dim+1))) << std::endl;
    std::cout << "L2Error between the 2 cylinder is : " << std::sqrt(l2errorU_4/(count_1)) << std::endl;
    L2ErrorU_.push_back(std::sqrt(l2errorU));
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runMMS()
{

    exact_solution = new ExactSolutionTaylorCouette<dim>;
    forcing_function = new NoForce<dim>;
    viscosity_=1;
    radius=0.21;
    radius_2=0.91;
    speed=1;
    couette= false;
    pressure_link=false;
    make_cube_grid(initialSize_);
    setup_dofs();

    std::cout  << "reynolds for the cylinder : " << speed*radius*2/viscosity_<< std::endl;

//    compute_initial_guess();
    for (unsigned int cycle =0; cycle < 7 ; cycle++)
    {
        if (cycle !=0) refine_mesh();
        std::cout  << "cycle: " << cycle << std::endl;
        newton_iteration(1.e-6, 10, true, true);
        output_results (cycle);
        torque();
        if (couette==true){

        calculateL2Error();
        }

    }
    std::ofstream output_file("./L2Error.dat");
    for (unsigned int i=0 ; i < L2ErrorU_.size() ; ++i)
    {
        output_file << i+initialSize_ << " " << L2ErrorU_[i] << std::endl;
    }
    output_file.close();
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runCouette()
{
    viscosity_=10;
    GridIn<dim> grid_in;
    grid_in.attach_triangulation (triangulation);
    std::ifstream input_file("taylorcouette.msh");

    grid_in.read_msh(input_file);


    static const SphericalManifold<dim> boundary;

    triangulation.set_all_manifold_ids_on_boundary(0);
    triangulation.set_manifold (0, boundary);

    forcing_function = new NoForce<dim>;
    exact_solution = new ExactSolutionTaylorCouette<dim>;
    setup_dofs();

    for (int cycle=0 ; cycle < 6 ; cycle++)
    {
        if (cycle !=0)  refine_mesh();
        newton_iteration(1.e-10, 50, true, true);
        output_results (cycle);
        calculateL2Error();
    }

    std::ofstream output_file("./L2Error.dat");
    for (unsigned int i=0 ; i < L2ErrorU_.size() ; ++i)
    {
        output_file << i+initialSize_ << " " << L2ErrorU_[i] << std::endl;
    }
    output_file.close();
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runCouette_sharp()
{
    make_cube_grid(initialSize_);
    exact_solution = new ExactSolutionMMS<dim>;
    forcing_function = new NoForce<dim>;
    viscosity_=0.000001;
    setup_dofs();


//    compute_initial_guess();6
    for (unsigned int cycle =0; cycle < 2 ; cycle++)
    {
        if (cycle !=0) refine_mesh();
        std::cout  << "cycle : " << 1 << std::endl;
        newton_iteration(1.e-6, 1, true, true);
        output_results (cycle);
        calculateL2Error();

    }
    std::ofstream output_file("./L2Error.dat");
    for (unsigned int i=0 ; i < L2ErrorU_.size() ; ++i)
    {
        output_file << i+initialSize_ << " " << L2ErrorU_[i] << std::endl;
    }
    output_file.close();
}

int main (int argc, char *argv[])
{
	// À discuter avec Bruno
	Utilities::MPI::MPI_InitFinalize mpi_initialization(
        argc, argv, numbers::invalid_unsigned_int);
	// Fin de discussion avec Bruno
	
    try
    {
        DirectSteadyNavierStokes<2> problem_2d(1,1);
        //problem_2d.runCouette();
        problem_2d.runMMS();
    }
    catch (std::exception &exc)
    {
        std::cerr << std::endl << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        std::cerr << "Exception on processing: " << std::endl
                  << exc.what() << std::endl
                  << "Aborting!" << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << std::endl << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        std::cerr << "Unknown exception!" << std::endl
                  << "Aborting!" << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        return 1;
    }
    return 0;
}
