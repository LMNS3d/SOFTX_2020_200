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
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/lac/affine_constraints.h>
#include <deal.II/lac/sparse_direct.h>

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

#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/solution_transfer.h>

#include <deal.II/grid/grid_tools.h>

#include "exactsolutions.h"
#include "forcingfunctions.h"
#include "boundaryconditions.h"


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
    void sharp_edge_V2();
    void refine_mesh_uniform();
    void setup_dofs();
    void initialize_system();
    void torque();

    void assemble(const bool initial_step,
                   const bool assemble_matrix);
    void assemble_system(const bool initial_step);
    void assemble_rhs(const bool initial_step);
    void solve(bool initial_step);
    void calculateL2Error();
    void output_results(const unsigned int cycle) const;
    void newton_iteration(const double tolerance,
                          const unsigned int max_iteration,
                          const bool is_initial_step,
                          const bool output_result);


    std::vector<types::global_dof_index> dofs_per_block;

    double viscosity_;
    const unsigned int           degreeIntegration_;
    Triangulation<dim> triangulation;
    FESystem<dim> fe;
    DoFHandler<dim> dof_handler;


    AffineConstraints<double>    zero_constraints;
    AffineConstraints<double>    nonzero_constraints;

    BlockSparsityPattern         sparsity_pattern;
    BlockSparseMatrix<double>    system_matrix;

    BlockVector<double>          present_solution;
    BlockVector<double>          newton_update;
    BlockVector<double>          system_rhs;
    BlockVector<double>          evaluation_point;

    Vector<double> immersed_x;
    Vector<double> immersed_y;
    Vector<double> immersed_value;
    Vector<double> immersed_it;

    const SimulationCases simulationCase_=MMS;
    const bool stabilized_=false;
    const bool iterative_=false;
    std::vector<double> L2ErrorU_;
    const int initialSize_=4;
};


// Constructor
template<int dim>
DirectSteadyNavierStokes<dim>::DirectSteadyNavierStokes(const unsigned int degreeVelocity, const unsigned int degreePressure):
    viscosity_(1), degreeIntegration_(degreeVelocity),
    fe(FE_Q<dim>(degreeVelocity), dim, FE_Q<dim>(degreePressure), 1),
    dof_handler(triangulation)
{}


template <int dim>
DirectSteadyNavierStokes<dim>::~DirectSteadyNavierStokes ()
{
  triangulation.clear ();
}



template <int dim>
void DirectSteadyNavierStokes<dim>::make_cube_grid (int refinementLevel)
{
  GridGenerator::hyper_cube (triangulation, -1, 1);
  //const Point<2> center_immersed(0,0);
  //GridGenerator::hyper_ball(triangulation,center_immersed,1);
  triangulation.refine_global (7);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_grid()
{
    triangulation.refine_global(1);
}


template <int dim>
void DirectSteadyNavierStokes<dim>::setup_dofs ()
{
    system_matrix.clear();

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

      DoFTools::make_hanging_node_constraints(dof_handler, nonzero_constraints);
      VectorTools::interpolate_boundary_values(dof_handler, 1, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                               fe.component_mask(velocities));

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

template <int dim>
void DirectSteadyNavierStokes<dim>::initialize_system()
{
  {
    BlockDynamicSparsityPattern dsp (dofs_per_block, dofs_per_block);
    DoFTools::make_flux_sparsity_pattern (dof_handler, dsp, nonzero_constraints);
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
                        if (false)
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
                if (false)
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
  const AffineConstraints<double> &constraints_used = initial_step ? nonzero_constraints : zero_constraints;
  SparseDirectUMFPACK direct;
  direct.initialize(system_matrix);
  direct.vmult(newton_update,system_rhs);
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
                                                     0.15, 0.0);
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
void DirectSteadyNavierStokes<dim>::sharp_edge() {
    unsigned int nb_immersed=10000;
    immersed_x.reinit(nb_immersed);
    immersed_y.reinit(nb_immersed);
    immersed_value.reinit(nb_immersed);
    using numbers::PI;
    const double center_x=0;
    const double center_y=0;


    const Point<2> center_immersed(center_x,center_y);
    double radius=0.21;
    double radius_2=0.61;

    for (unsigned int i=0 ;i <nb_immersed;++i){
        immersed_x(i)=radius*cos(i*2*PI/(nb_immersed/2))+center_x;
        immersed_y(i)=radius*sin(i*2*PI/(nb_immersed/2))+center_y;
        immersed_value(i)=0;
    }
    for (unsigned int i=nb_immersed/2 ;i <nb_immersed;++i){
        immersed_x(i)=radius_2*cos(i*2*PI/(nb_immersed/2))+center_x;
        immersed_y(i)=radius_2*sin(i*2*PI/(nb_immersed/2))+center_y;
        immersed_value(i)=0;
    }

    immersed_it.reinit(dof_handler.n_dofs());

// overwrite the line for the point in mesh
    MappingQ1<dim> immersed_map;
    std::map< types::global_dof_index, Point< dim >>  	support_points;
    DoFTools::map_dofs_to_support_points(immersed_map,dof_handler,support_points);

    QGauss<dim> q_formula(fe.degree+1);
    // we need to define what part of the finite elements we need to compute in orther to solve the equation we want
    // in or case wee need the gradient of the shape function the jacobians of the matrix and the shape function values
    FEValues<dim> fe_values(fe, q_formula,update_quadrature_points);
    FEValues<dim> fe_values_2(fe, q_formula,update_quadrature_points);
    const unsigned int n_q_points = q_formula.size();
    const unsigned int dofs_per_cell = fe.dofs_per_cell;
    std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_2(dofs_per_cell);

    unsigned int best_vertex = 0;

    std::vector<Point<dim>> support_point(dof_handler.n_dofs());
    const auto &cell_iterator=dof_handler.active_cell_iterators();

    double min_cell_d=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    std::cout << "min cell dist: " << min_cell_d << std::endl;
    for (const auto &cell : cell_iterator)  {
        fe_values.reinit(cell);
        cell->get_dof_indices(local_dof_indices);
        for (unsigned int kk = 0; kk< 2; ++kk) {
            for (unsigned int q_point = kk; q_point < local_dof_indices.size(); q_point += dim + 1) {
                Point<dim> vertices_ib_j(immersed_x(0), immersed_y(0));
                double best_dist_ib = sqrt((support_points[local_dof_indices[q_point]] - vertices_ib_j).norm_square());
                double ib_value_select = 1;
                Tensor<1, 2, double> best_vect_dist = (support_points[local_dof_indices[q_point]] - vertices_ib_j);
                for (unsigned int j = 0; j < immersed_x.size(); j++) {
                    Point<dim> vertices_ib_j(immersed_x(j), immersed_y(j));
                    Tensor<1, 2, double> vect_dist = (support_points[local_dof_indices[q_point]] - vertices_ib_j);
                    double dist = sqrt(vect_dist[1] * vect_dist[1] + vect_dist[0] * vect_dist[0]);
                    //Tensor<1,2 < double>>
                    if (dist < best_dist_ib) {
                        best_vect_dist = vect_dist;
                        best_dist_ib = dist;
                        ib_value_select = immersed_value(j);
                    }
                }
                unsigned int global_index_overrigth = local_dof_indices[q_point];
                if (best_dist_ib < min_cell_d & immersed_it[global_index_overrigth] <= 2) {


                 //   std::cout << "best dist: " << best_dist_ib << std::endl;
                   // std::cout << "position of dof: " << support_points[local_dof_indices[q_point]] << std::endl;
                    //std::cout << "index global of dof: " << local_dof_indices[q_point] << std::endl;
                    //std::cout << "DOFS per cell: " << fe.dofs_per_cell << std::endl;


                  //  for(unsigned int j=0 ;j<fe.dofs_per_cell;++j){
                       // std::cout << "index global of local dof: " << local_dof_indices[j] << std::endl;
                    //}

                    if (best_dist_ib != 0) {

                        const Point<dim> second_point(support_points[local_dof_indices[q_point]] + best_vect_dist);
                        const Point<dim> ib_point(support_points[local_dof_indices[q_point]] - best_vect_dist);
                        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, second_point);


                        cell_2->get_dof_indices(local_dof_indices);
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                       // std::cout << "CELL_2" << std::endl;

                        //for(unsigned int j=0 ;j<fe.dofs_per_cell;++j){
                        //    std::cout << "index global of local dof: " << local_dof_indices[j] << std::endl;
                        //}

                        for (unsigned int j = 0; j < dof_handler.n_dofs(); j++)
                            system_matrix.set(global_index_overrigth, j, 0);

                        system_matrix.add(global_index_overrigth, global_index_overrigth,
                                          -2 / (best_dist_ib * best_dist_ib));
                        immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;

                        for (unsigned int j = kk; j < fe.dofs_per_cell; j+=dim+1) {
                            //td::cout << "index global of dof: " << local_dof_indices[j]<< std::endl;

                            system_matrix.add(global_index_overrigth, local_dof_indices[j],
                                              fe.shape_value(j, second_point_v) / (best_dist_ib * best_dist_ib));
                        }
                        if(kk==0)
                        {
                            if(ib_point.norm()<=radius)
                                system_rhs(global_index_overrigth) =  -ib_point[1]/radius/ (best_dist_ib * best_dist_ib);
                            else
                                system_rhs(global_index_overrigth) =  0/ (best_dist_ib * best_dist_ib);
                        }
                        else{
                            if(ib_point.norm()<=radius)
                                system_rhs(global_index_overrigth) =  ib_point[0]/radius/ (best_dist_ib * best_dist_ib);
                            else
                                system_rhs(global_index_overrigth) =  0/ (best_dist_ib * best_dist_ib);
                        }


                    } else {
                        for (unsigned int j = kk; j < fe.dofs_per_cell; j+=dim+1)
                            system_matrix.set(global_index_overrigth, j, 0);

                        system_matrix.add(global_index_overrigth, global_index_overrigth, 1);
                        system_rhs(global_index_overrigth) = 0;
                        immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;

                    }
                }
                /*if (support_points[local_dof_indices[q_point]][0] == -1 & immersed_it[global_index_overrigth] <= 2) {
                    unsigned int global_index_overrigth = local_dof_indices[q_point];
                    for (unsigned int j = 0; j < dof_handler.n_dofs(); j++)
                        system_matrix.set(global_index_overrigth, j, 0);

                    system_matrix.add(global_index_overrigth, global_index_overrigth, 1);
                    system_rhs(global_index_overrigth) = 0;
                    immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;
                }
                if (support_points[local_dof_indices[q_point]][0] == 1 & immersed_it[global_index_overrigth] <= 2) {
                    unsigned int global_index_overrigth = local_dof_indices[q_point];
                    for (unsigned int j = 0; j < dof_handler.n_dofs(); j++)
                        system_matrix.set(global_index_overrigth, j, 0);

                    system_matrix.add(global_index_overrigth, global_index_overrigth, 1);
                    system_rhs(global_index_overrigth) = 0;
                    immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;
                }
                if (support_points[local_dof_indices[q_point]][1] == 1 & immersed_it[global_index_overrigth] <= 2) {
                    unsigned int global_index_overrigth = local_dof_indices[q_point];
                    for (unsigned int j = 0; j < dof_handler.n_dofs(); j++)
                        system_matrix.set(global_index_overrigth, j, 0);

                    system_matrix.add(global_index_overrigth, global_index_overrigth, 1);
                    system_rhs(global_index_overrigth) = 0;
                    immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;
                }
                if (support_points[local_dof_indices[q_point]][1] == -1 & immersed_it[global_index_overrigth] <= 2) {
                    unsigned int global_index_overrigth = local_dof_indices[q_point];
                    for (unsigned int j = 0; j < dof_handler.n_dofs(); j++)
                        system_matrix.set(global_index_overrigth, j, 0);

                    system_matrix.add(global_index_overrigth, global_index_overrigth, 1);
                    system_rhs(global_index_overrigth) = 0;
                    immersed_it[global_index_overrigth] = immersed_it[global_index_overrigth] + 1;
                }*/
            }
        }
    }
}
template <int dim>
void DirectSteadyNavierStokes<dim>::sharp_edge_V2() {
    unsigned int nb_immersed=10000;
    immersed_x.reinit(nb_immersed);
    immersed_y.reinit(nb_immersed);
    immersed_value.reinit(nb_immersed);
    using numbers::PI;
    const double center_x=0;
    const double center_y=0;
    const double speed=1;

    const Point<2> center_immersed(center_x,center_y);
    double radius=0.21;
    double radius_2=0.91;
    bool couette=true;

    // overwrite the line for the point in mesh
    MappingQ1<dim> immersed_map;
    std::map< types::global_dof_index, Point< dim >>  	support_points;
    DoFTools::map_dofs_to_support_points(immersed_map,dof_handler,support_points);

    QGauss<dim> q_formula(fe.degree+1);
    // we need to define what part of the finite elements we need to compute in orther to solve the equation we want
    // in or case wee need the gradient of the shape function the jacobians of the matrix and the shape function values
    FEValues<dim> fe_values(fe, q_formula,update_quadrature_points);
    FEValues<dim> fe_values_2(fe, q_formula,update_quadrature_points);
    const unsigned int n_q_points = q_formula.size();
    const unsigned int dofs_per_cell = fe.dofs_per_cell;
    std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_2(dofs_per_cell);
    std::vector<types::global_dof_index> local_dof_indices_3(dofs_per_cell);

    unsigned int best_vertex = 0;

    std::vector<Point<dim>> support_point(dof_handler.n_dofs());
    const auto &cell_iterator=dof_handler.active_cell_iterators();

    double min_cell_d=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    std::cout << "min cell dist: " << min_cell_d << std::endl;
    unsigned int bridge_gros=1;
    unsigned int bridge_petit=1;
    for (const auto &cell : cell_iterator)  {

        fe_values.reinit(cell);
        cell->get_dof_indices(local_dof_indices);
        unsigned int count_small=0;
        unsigned int count_large=0;
        for (unsigned int j = 0; j < local_dof_indices.size(); ++j) {

            if ((support_points[local_dof_indices[j]]-center_immersed).norm()<=radius){
                ++count_small;
            }
            if ((support_points[local_dof_indices[j]]-center_immersed).norm()<=radius_2){
                ++count_large;
            }
            if( couette==false) {
                if (j != 2 and j != 5 and j != 8 and j != 11) {
                    if (support_points[local_dof_indices[j]][0] == -1) {
                        unsigned int global_index_overrigth = local_dof_indices[j];
                        for (unsigned int k = 0; k < dof_handler.n_dofs(); k++)
                            system_matrix.set(global_index_overrigth, k, 0);

                        system_matrix.set(global_index_overrigth, global_index_overrigth, 1);
                        if (j == 0 or j == 3 or j == 6 or j == 9)
                            system_rhs(global_index_overrigth) = 1*speed;
                        if (j == 1 or j == 4 or j == 7 or j == 10)
                            system_rhs(global_index_overrigth) = 0;
                    }
                    /*if (support_points[local_dof_indices[j]][0] == 1) {
                        unsigned int global_index_overrigth = local_dof_indices[j];

                        for (unsigned int k = 0; k < dof_handler.n_dofs(); k++)
                            system_matrix.set(global_index_overrigth, k, 0);

                        system_matrix.set(global_index_overrigth, global_index_overrigth, 1);
                        system_rhs(global_index_overrigth) = 0.707*speed;
                    }*/
                    if (support_points[local_dof_indices[j]][1] == -1) {
                        unsigned int global_index_overrigth = local_dof_indices[j];

                        for (unsigned int k = 0; k < dof_handler.n_dofs(); k++)
                            system_matrix.set(global_index_overrigth, k, 0);

                        system_matrix.set(global_index_overrigth, global_index_overrigth, 1);
                        if (j == 0 or j == 3 or j == 6 or j == 9)
                            system_rhs(global_index_overrigth) = 1*speed;
                        if (j == 1 or j == 4 or j == 7 or j == 10)
                            system_rhs(global_index_overrigth) = 0;
                    }

                    if (support_points[local_dof_indices[j]][1] == 1) {
                        unsigned int global_index_overrigth = local_dof_indices[j];

                        for (unsigned int k = 0; k < dof_handler.n_dofs(); k++)
                            system_matrix.set(global_index_overrigth, k, 0);

                        system_matrix.set(global_index_overrigth, global_index_overrigth, 1);
                        if (j == 0 or j == 3 or j == 6 or j == 9)
                            system_rhs(global_index_overrigth) = 1*speed;
                        if (j == 1 or j == 4 or j == 7 or j == 10)
                            system_rhs(global_index_overrigth) = 0;
                    }
                }
            }

        }
        if (couette==false){
        count_large=0;
        }
       // std::cout << "count small: " << count_small<< std::endl;
        //std::cout << "count large: " << count_large<< std::endl;
        if (count_small!=0 and count_small!=local_dof_indices.size()){
            //cellule coupe par boundary small
            for (unsigned int k = 0; k< 3; ++k) {
                if (k < 2) {
                    unsigned int l = k;
                    while (l < local_dof_indices.size()) {
                        Tensor<1, 2, double> vect_dist = (support_points[local_dof_indices[l]] - radius *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());
                        //std::cout << "dist_boundary: " << support_points[local_dof_indices[l]]<< std::endl;
                        //std::cout << "dist_boundary: " << radius*(support_points[local_dof_indices[l]]-center_immersed)/(support_points[local_dof_indices[l]]-center_immersed).norm()<< std::endl;
                        //std::cout << "dist_boundary: " << vect_dist<< std::endl;
                        double dist = sqrt(vect_dist[1] * vect_dist[1] + vect_dist[0] * vect_dist[0]);
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + vect_dist);
                        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, second_point);
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);

                        unsigned int global_index_overrigth = local_dof_indices[l];
                        for (unsigned int m = 0; m < dof_handler.n_dofs(); m++)
                            system_matrix.set(global_index_overrigth, m, 0);
                        system_matrix.set(global_index_overrigth, global_index_overrigth,
                                          -2 / (dist * dist));
                        unsigned int n = k;
                        while (n < local_dof_indices_2.size()) {
                            //td::cout << "index global of dof: " << local_dof_indices[j]<< std::endl;
                            system_matrix.add(global_index_overrigth, local_dof_indices_2[n],
                                              fe.shape_value(n, second_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }
                        if (couette==true)
                        {

                        if (k == 0) {
                            system_rhs(global_index_overrigth) = 1 * ((support_points[local_dof_indices[l]] -
                                                                       center_immersed) /
                                                                      (support_points[local_dof_indices[l]] -
                                                                       center_immersed).norm())[1] / (dist * dist);
                        }
                        else {
                            system_rhs(global_index_overrigth) = -1 * ((support_points[local_dof_indices[l]] -
                                                                        center_immersed) /
                                                                       (support_points[local_dof_indices[l]] -
                                                                        center_immersed).norm())[0] / (dist * dist);
                        }
                        }
                        if (couette==false)
                            system_rhs(global_index_overrigth)=0;

                        if (l < (dim + 1) * 4) {
                            l = l + dim + 1;
                        } else {
                            l = l + dim;
                        }
                    }

                }
                else if(k<3 and bridge_petit==0){
                    //bridge_petit=1;
                    unsigned int l = k;
                    while (l < 12) {
                        Tensor<1, 2, double> vect_dist = (support_points[local_dof_indices[l]] - radius *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());

                        double dist = sqrt(vect_dist[1] * vect_dist[1] + vect_dist[0] * vect_dist[0]);
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + 1*vect_dist);
                        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, second_point);
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);

                        const Point<dim> third_point(support_points[local_dof_indices[l]] - 1*vect_dist);
                        const auto &cell_3 = GridTools::find_active_cell_around_point(dof_handler, third_point);
                        Point<dim> third_point_v = immersed_map.transform_real_to_unit_cell(cell_3, third_point);
                        cell_3->get_dof_indices(local_dof_indices_3);

                        unsigned int global_index_overrigth = local_dof_indices[l];

                        for (unsigned int m = 0; m < dof_handler.n_dofs(); m++)
                            system_matrix.set(global_index_overrigth, m, 0);
                        system_matrix.set(global_index_overrigth, global_index_overrigth,
                                          -2 / (dist * dist));
                        unsigned int n = k;
                        while (n < 12) {

                            system_matrix.add(global_index_overrigth ,local_dof_indices_3[n],fe.shape_value(n, third_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }

                        n = k;
                        while (n < 12) {
                            system_matrix.add(global_index_overrigth ,local_dof_indices_2[n],fe.shape_value(n, second_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }


                        if (k == 0) {
                            system_rhs(global_index_overrigth) = 0;
                        } else {
                            system_rhs(global_index_overrigth) = 0;
                        }


                        if (l < (dim + 1) * 4) {
                            l = l + dim + 1;
                        } else {
                            l = l + dim;
                        }
                    }
                }

                }
            }

        if (count_large!=0 and count_large!=local_dof_indices.size()){
            //cellule coupe par boundary large
            for (unsigned int k = 0; k< 3; ++k) {
                if (k < 2) {
                    unsigned int l = k;
                    while (l < local_dof_indices.size()) {
                        Tensor<1, 2, double> vect_dist = (support_points[local_dof_indices[l]] - radius_2 *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());
                        //std::cout << "dist_boundary: " << support_points[local_dof_indices[l]]<< std::endl;
                        //std::cout << "dist_boundary: " << radius*(support_points[local_dof_indices[l]]-center_immersed)/(support_points[local_dof_indices[l]]-center_immersed).norm()<< std::endl;
                        //std::cout << "dist_boundary: " << vect_dist<< std::endl;
                        double dist = sqrt(vect_dist[1] * vect_dist[1] + vect_dist[0] * vect_dist[0]);
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + vect_dist);
                        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, second_point);
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);

                        unsigned int global_index_overrigth = local_dof_indices[l];
                        for (unsigned int m = 0; m < dof_handler.n_dofs(); m++)
                            system_matrix.set(global_index_overrigth, m, 0);
                        system_matrix.set(global_index_overrigth, global_index_overrigth,
                                          -2 / (dist * dist));
                        unsigned int n = k;
                        while (n < local_dof_indices_2.size()) {
                            //td::cout << "index global of dof: " << local_dof_indices[j]<< std::endl;
                            system_matrix.add(global_index_overrigth, local_dof_indices_2[n],
                                              fe.shape_value(n, second_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }

                        if (k == 0) {
                            system_rhs(global_index_overrigth) = 0;
                        } else {
                            system_rhs(global_index_overrigth) = 0;
                        }


                        if (l < (dim + 1) * 4) {
                            l = l + dim + 1;
                        } else {
                            l = l + dim;
                        }
                    }

                }
                else if(k<3 and bridge_gros==0){
                    //bridge_gros=1;
                    unsigned int l = k;
                    while (l < 12) {
                        Tensor<1, 2, double> vect_dist = (support_points[local_dof_indices[l]] - radius_2 *
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed) /
                                                                                                 (support_points[local_dof_indices[l]] -
                                                                                                  center_immersed).norm());

                        double dist = sqrt(vect_dist[1] * vect_dist[1] + vect_dist[0] * vect_dist[0]);
                        const Point<dim> second_point(support_points[local_dof_indices[l]] + 1*vect_dist);
                        const auto &cell_2 = GridTools::find_active_cell_around_point(dof_handler, second_point);
                        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell_2, second_point);
                        cell_2->get_dof_indices(local_dof_indices_2);

                        const Point<dim> third_point(support_points[local_dof_indices[l]] - 1*vect_dist);
                        const auto &cell_3 = GridTools::find_active_cell_around_point(dof_handler, third_point);
                        Point<dim> third_point_v = immersed_map.transform_real_to_unit_cell(cell_3, third_point);
                        cell_3->get_dof_indices(local_dof_indices_3);

                        unsigned int global_index_overrigth = local_dof_indices[l];

                        for (unsigned int m = 0; m < dof_handler.n_dofs(); m++)
                            system_matrix.set(global_index_overrigth, m, 0);
                        system_matrix.set(global_index_overrigth, global_index_overrigth,
                                          -2 / (dist * dist));
                        unsigned int n = k;
                        while (n < 12) {

                            system_matrix.add(global_index_overrigth ,local_dof_indices_3[n],fe.shape_value(n, third_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }

                        n = k;
                        while (n < 12) {
                            system_matrix.add(global_index_overrigth ,local_dof_indices_2[n],fe.shape_value(n, second_point_v) / (dist * dist));
                            if (n < (dim + 1) * 4) {
                                n = n + dim + 1;
                            } else {
                                n = n + dim;
                            }
                        }


                        if (k == 0) {
                            system_rhs(global_index_overrigth) = 0;
                        } else {
                            system_rhs(global_index_overrigth) = 0;
                        }


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
    for (unsigned int m = 0; m < dof_handler.n_dofs(); m++)
        system_matrix.set(dof_handler.n_dofs()-1, m, 0);
    system_matrix.set(dof_handler.n_dofs()-1,dof_handler.n_dofs()-1,1/(min_cell_d*min_cell_d));
    system_rhs(dof_handler.n_dofs()-1)=0;

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
    double radius=0.21;
    double radius_2=0.91;
    double mu=1;

    MappingQ1<dim> immersed_map;
    std::vector<types::global_dof_index> local_dof_indices(fe.dofs_per_cell);
    unsigned int nb_evaluation=100;
    double t_torque=0;
    double t_torque_l=0;

    double T_in=0;
    double dr=(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation))/sqrt(2*(GridTools::minimal_cell_diameter(triangulation)*GridTools::minimal_cell_diameter(triangulation)));
    for (unsigned int i=0;i<nb_evaluation;++i ) {
        const Point<2> eval_point(radius * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius * sin(i * 2 * PI / (nb_evaluation)) + center_y);
        const auto &cell = GridTools::find_active_cell_around_point(dof_handler, eval_point);
        Point<dim> second_point_v = immersed_map.transform_real_to_unit_cell(cell, eval_point);
        cell->get_dof_indices(local_dof_indices);
        double u_1=0;
        double v_1=0;
        /*for (unsigned int j=0;j<12;j=j+3 ){
            u_1+=fe.shape_value(j,second_point_v)*present_solution(local_dof_indices[j]);
            v_1+=fe.shape_value(j+1,second_point_v)*present_solution(local_dof_indices[j+1]);
        }*/
        u_1=-sin(i * 2 * PI / (nb_evaluation));
        v_1=cos(i * 2 * PI / (nb_evaluation));
        double U1=u_1*cos(i * 2 * PI / (nb_evaluation)-PI/2)+v_1*sin(i * 2 * PI / (nb_evaluation)-PI/2);

        const Point<2> eval_point_2(eval_point[0]+dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]+dr*sin(i * 2 * PI / (nb_evaluation)));
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
        std::cout << "du_dr " <<du_dr << std::endl;
        std::cout << "local shear stress: " <<du_dr*mu*radius << std::endl;
        t_torque+=radius*du_dr*mu*radius*2*PI*radius/(nb_evaluation-1) ;

    }

    std::cout << "total_torque_small " << t_torque << std::endl;

    for (unsigned int i=0;i<nb_evaluation;++i ) {
        const Point<2> eval_point(radius_2 * cos(i * 2 * PI / (nb_evaluation)) + center_x,radius_2 * sin(i * 2 * PI / (nb_evaluation)) + center_y);
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
        const Point<2> eval_point_2(eval_point[0]-dr*cos(i * 2 * PI / (nb_evaluation)),eval_point[1]-dr*sin(i * 2 * PI / (nb_evaluation)));
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

        std::cout << "local shear stress: " <<radius_2*du_dr*mu << std::endl;
        t_torque_l+=radius_2*du_dr*mu*radius_2*2*PI*radius_2/(nb_evaluation-1) ;

    }
    std::cout << "total_torque_large" << t_torque_l << std::endl;
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
              sharp_edge_V2();
              current_res = system_rhs.l2_norm();
              std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res << std::endl;
              solve(first_step);
              present_solution = newton_update;
              nonzero_constraints.distribute(present_solution);
              first_step = false;
              evaluation_point = present_solution;
              assemble_rhs(first_step);
              current_res = system_rhs.l2_norm();
              last_res = current_res;
            }
          else
            {
              std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res << std::endl;
              evaluation_point = present_solution;
              assemble_system(first_step);
              sharp_edge_V2();
              solve(first_step);
              for (double alpha = 1.0; alpha > 1e-3; alpha *= 0.5)
                {
                  evaluation_point = present_solution;
                  evaluation_point.add(alpha, newton_update);
                  nonzero_constraints.distribute(evaluation_point);
                  assemble_rhs(first_step);
                  current_res = system_rhs.l2_norm();
                  std::cout << "\t\talpha = " << std::setw(6) << alpha << std::setw(0)
                            << " res = " << current_res << std::endl;
                  //if (current_res < last_res)
                   // break;
                }
              {
                present_solution = evaluation_point;
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


    const unsigned int   			dofs_per_cell = fe.dofs_per_cell;         // This gives you dofs per cell
    std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell); //  Local connectivity

    const unsigned int   n_q_points    = quadrature_formula.size();
    double l2errorU=0.;

    std::vector<Vector<double> > q_exactSol (n_q_points, Vector<double>(dim+1));


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
    for (; cell!=endc; ++cell)
    {
        fe_values.reinit (cell);
        fe_values[velocities].get_function_values (present_solution,
                                                   local_velocity_values);
        fe_values[pressure].get_function_values(present_solution,
                                               local_pressure_values);

        //Retrieve the effective "connectivity matrix" for this element
        cell->get_dof_indices (local_dof_indices);

        // Get the exact solution at all gauss points
        exact_solution->vector_value_list(fe_values.get_quadrature_points(),
                                            q_exactSol);

        for(unsigned int q=0; q<n_q_points; q++)
        {
            //Find the values of x and u_h (the finite element solution) at the quadrature points
            double ux_sim=local_velocity_values[q][0];
            double ux_exact=q_exactSol[q][0];

            double uy_sim=local_velocity_values[q][1];
            double uy_exact=q_exactSol[q][1];

            l2errorU += (ux_sim-ux_exact)*(ux_sim-ux_exact) * fe_values.JxW(q);
            l2errorU += (uy_sim-uy_exact)*(uy_sim-uy_exact) * fe_values.JxW(q);
        }
    }
    std::cout << "L2Error is : " << std::sqrt(l2errorU) << std::endl;
    L2ErrorU_.push_back(std::sqrt(l2errorU));
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runMMS()
{
    make_cube_grid(initialSize_);
    exact_solution = new ExactSolutionMMS<dim>;
    forcing_function = new NoForce<dim>;
    viscosity_=1;
    setup_dofs();


//    compute_initial_guess();
    for (unsigned int cycle =0; cycle < 1 ; cycle++)
    {
        if (cycle !=0) refine_mesh_uniform();
        newton_iteration(1.e-6, 2, true, true);
        output_results (cycle);
        torque();
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

    for (int cycle=0 ; cycle < 4 ; cycle++)
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
    viscosity_=1.;
    setup_dofs();


//    compute_initial_guess();6
    for (unsigned int cycle =0; cycle < 4 ; cycle++)
    {
        if (cycle !=0) refine_mesh_uniform();
        newton_iteration(1.e-6, 5, true, true);
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

int main ()
{
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