/* -*- mode: c++; coding: utf-8; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; show-trailing-whitespace: t  -*-

 This file is part of the Feel++ library

 Author(s): Christophe Prud'homme <christophe.prudhomme@feelpp.org>
 Date: 16 Feb 2020

 Copyright (C) 2020 Feel++ Consortium

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#pragma once
//#include <boost/container_hash/hash.hpp>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <mmg/libmmg.h>
#include <parmmg/libparmmg.h>
#include <variant>
#include <feel/feelmesh/concatenate.hpp>

namespace Feel
{
/**
 * @brief  a pairing function is a process to uniquely encode two natural numbers into a single natural number.
 * 
 * @param a 
 * @param b 
 * @return constexpr int 
 */
constexpr int cantor_pairing( int a, int b )
{
    return ( a + b ) * ( a + b + 1 ) / 2 + b;
}
/**
 * @brief compute cantor pairing recursively for number of natural numbers > 2
 * 
 * @tparam T integrer type
 * @param a integer to be paired with
 * @param b integer to be paired with
 * @param ts remaining pack that will be treated recursively
 * @return constexpr int 
 */
template <typename... T>
constexpr int cantor_pairing( int a, int b, T&&... ts )
{
    return cantor_pairing( cantor_pairing( a, b ), ts... );
}

enum class RemeshMode {
    MMG=1,
    PARMMG=2
};
/**
 * @brief Class that handles remeshing in sequential using mmg and parallel using parmmg
 * 
 * @tparam MeshType mesh type to be remeshed
 */
template <typename MeshType>
class Remesh
{
  public:
    using mmg_mesh_t = std::variant<MMG5_pMesh, PMMG_pParMesh>;
    using mesh_t = MeshType;
    using mesh_ptrtype = std::shared_ptr<MeshType>;
    using mesh_ptr_t = mesh_ptrtype;
    using scalar_metric_t = typename Pch_type<mesh_t, 1>::element_type;
    Remesh()
        : Remesh( nullptr )
    {
    }
    Remesh( std::shared_ptr<MeshType> const& mesh,
            boost::any const& required_element_markers,
            boost::any const& required_facet_markers );

    ~Remesh()
    {
        if ( std::holds_alternative<MMG5_pMesh>( M_mmg_mesh ) )
        {
            if constexpr ( dimension_v<MeshType> == 3 )
                MMG3D_Free_all( MMG5_ARG_start,
                                MMG5_ARG_ppMesh, &std::get<MMG5_pMesh>( M_mmg_mesh ), MMG5_ARG_ppMet, &M_mmg_sol,
                                MMG5_ARG_end );
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
                MMGS_Free_all( MMG5_ARG_start,
                               MMG5_ARG_ppMesh, &std::get<MMG5_pMesh>( M_mmg_mesh ), MMG5_ARG_ppMet, &M_mmg_sol,
                               MMG5_ARG_end );
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
                MMG2D_Free_all( MMG5_ARG_start,
                                MMG5_ARG_ppMesh, &std::get<MMG5_pMesh>( M_mmg_mesh ), MMG5_ARG_ppMet, &M_mmg_sol,
                                MMG5_ARG_end );
        }
        else if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
        {
            PMMG_Free_all( PMMG_ARG_start,
                           PMMG_ARG_ppParMesh, &std::get<PMMG_pParMesh>( M_mmg_mesh ),
                           PMMG_ARG_end );
        }
    }

    /**
     * set scalar metric
     */
    void setMetric( scalar_metric_t const& );

    /**
     * execute remesh task
     */
    mesh_ptrtype execute(bool run = true)
    {
        if ( run )
        {
            if ( std::holds_alternative<MMG5_pMesh>( M_mmg_mesh ) )
            {
                if constexpr ( dimension_v<MeshType> == 3 )
                    MMG3D_mmg3dlib( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol );
                else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
                    MMGS_mmgslib( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol );
                else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
                    MMG2D_mmg2dlib( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol );
            }
            else if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
            {
                auto mesh = std::get<PMMG_pParMesh>( M_mmg_mesh );
                if ( M_mesh->worldCommPtr()->localSize() > 1 )
                {
                    int ier = PMMG_parmmglib_distributed( mesh );
                    if ( ier == PMMG_STRONGFAILURE )
                        throw std::logic_error( "Unable to adapt mesh in distributed mode." );
                }
                else
                {
                    int ier = PMMG_parmmglib_centralized( mesh );
                    if ( ier == PMMG_STRONGFAILURE )
                        throw std::logic_error( "Unable to adapt mesh in centralized mode." );
                }
            }
        }
        auto r = this->mmg2Mesh();
        return r;
    }

    /**
     * transform a Feel++ \p mesh into an Mmg mesh
     */
    mmg_mesh_t mesh2Mmg( std::shared_ptr<MeshType> const& m_in );
    mmg_mesh_t mesh2Mmg() { return mesh2Mmg( M_mesh ); }

    /**
     * convert a Mmg mesh into a Feel++ \p mesh
     */
    mesh_ptrtype mmg2Mesh( mmg_mesh_t const& m_in );
    mesh_ptrtype mmg2Mesh() { return mmg2Mesh( M_mmg_mesh ); }

  private:
    void setParameters();
    void setCommunicatorAPI();
    using neighbor_ent_t = std::map<int, std::pair<int,int>>;
    using local_face_index_t = std::vector<std::vector<int>>;
    using comm_t = std::tuple<neighbor_ent_t, local_face_index_t, std::map<int,int>>;
    comm_t getCommunicatorAPI();

  private:
    std::shared_ptr<MeshType> M_mesh;
    RemeshMode M_mode = RemeshMode::PARMMG;
    mmg_mesh_t M_mmg_mesh;
    MMG5_pSol M_mmg_sol;
    MMG5_pSol M_mmg_met;

    std::unordered_map<int, int> pt_id;
    std::unordered_map<int, std::pair<int, int>> face_id;

    boost::any M_required_element_markers;
    boost::any M_required_facet_markers;
    std::mutex mutex_;
};

template <typename MeshType>
Remesh<MeshType> remesher( std::shared_ptr<MeshType> const& m,
                           boost::any required_element_markers = std::vector<int>{},
                           boost::any required_facet_markers = std::vector<int>{} )
{
    return Remesh<MeshType>{ m, required_element_markers, required_facet_markers };
}

template <typename MeshType>
Remesh<MeshType>::Remesh( std::shared_ptr<MeshType> const& mesh,
                          boost::any const& required_element_markers,
                          boost::any const& required_facet_markers )
    : M_mesh( mesh ),
      M_mode( RemeshMode::MMG ),
      M_mmg_mesh(),
      M_mmg_sol( nullptr ),
      M_mmg_met( nullptr ),
      M_required_element_markers( required_element_markers ),
      M_required_facet_markers( required_facet_markers )
{
    if ( mesh->worldCommPtr()->localSize() == 1 && M_mode == RemeshMode::MMG)
    {
        MMG5_pMesh m = nullptr;
        if constexpr ( dimension_v<MeshType> == 3 )
            MMG3D_Init_mesh( MMG5_ARG_start,
                             MMG5_ARG_ppMesh, &m, MMG5_ARG_ppMet, &M_mmg_sol,
                             MMG5_ARG_end );
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
            MMGS_Init_mesh( MMG5_ARG_start,
                            MMG5_ARG_ppMesh, &m, MMG5_ARG_ppMet, &M_mmg_sol,
                            MMG5_ARG_end );
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
            MMG2D_Init_mesh( MMG5_ARG_start,
                             MMG5_ARG_ppMesh, &m, MMG5_ARG_ppMet, &M_mmg_sol,
                             MMG5_ARG_end );
        M_mmg_mesh = m;
    }
    else
    {

        PMMG_pParMesh m = nullptr;
        PMMG_Init_parMesh( PMMG_ARG_start,
                           PMMG_ARG_ppParMesh, &m,
                           PMMG_ARG_pMesh, PMMG_ARG_pMet,
                           PMMG_ARG_dim, mesh->realDimension(), PMMG_ARG_MPIComm, static_cast<MPI_Comm>( mesh->worldCommPtr()->comm() ),
                           PMMG_ARG_end );

        M_mmg_mesh = m;
        PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_verbose, 0 );
        PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_mmgVerbose, 1 );
        PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_debug, 5 );
        //PMMG_Set_dparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_DPARAM_hmin, 1e-2 );
    }

    this->mesh2Mmg();
    this->setParameters();
    this->setCommunicatorAPI();
}

template <typename MeshType>
void Remesh<MeshType>::setMetric( scalar_metric_t const& m )
{
    if ( std::holds_alternative<MMG5_pMesh>( M_mmg_mesh ) )
    {
        if constexpr ( dimension_v<MeshType> == 3 )
        {
            if ( MMG3D_Set_solSize( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol,
                                    MMG5_Vertex, M_mesh->numPoints(), MMG5_Scalar ) != 1 )
            {
                throw std::logic_error( "Unable to allocate the metric array." );
            }
        }
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
        {
            if ( MMGS_Set_solSize( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol,
                                   MMG5_Vertex, M_mesh->numPoints(), MMG5_Scalar ) != 1 )
            {
                throw std::logic_error( "Unable to allocate the metric array." );
            }
        }
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
        {
            if ( MMG2D_Set_solSize( std::get<MMG5_pMesh>( M_mmg_mesh ), M_mmg_sol,
                                    MMG5_Vertex, M_mesh->numPoints(), MMG5_Scalar ) != 1 )
            {
                throw std::logic_error( "Unable to allocate the metric array." );
            }
        }

        for ( auto const& welt : M_mesh->elements() )
        {
            auto const& [key, elt] = boost::unwrap_ref( welt );
            for ( auto const& ldof : m.functionSpace()->dof()->localDof( elt.id() ) )
            {
                size_type index = ldof.second.index();
                uint16_type local_dof = ldof.first.localDof();
                auto s = m( index );
                int pos = pt_id[elt.point( local_dof ).id()];
                if constexpr ( dimension_v<MeshType> == 3 )
                {
                    if ( MMG3D_Set_scalarSol( M_mmg_sol, s, pos ) != 1 )
                    {
                        throw std::logic_error( "Unable to set metric" );
                    }
                }
                else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
                {
                    if ( MMGS_Set_scalarSol( M_mmg_sol, s, pos ) != 1 )
                    {
                        throw std::logic_error( "Unable to set metric" );
                    }
                }
                else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
                {
                    if ( MMG2D_Set_scalarSol( M_mmg_sol, s, pos ) != 1 )
                    {
                        throw std::logic_error( "Unable to set metric" );
                    }
                }
            }
        }
    }
    if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
    {
        auto mesh = std::get<PMMG_pParMesh>( M_mmg_mesh );
        LOG(INFO) << fmt::format( " - setting metric  size: {}...", m.nDof() );
        if ( PMMG_Set_metSize( mesh, MMG5_Vertex, m.nLocalDof(), MMG5_Scalar ) != 1 )
        {
            fmt::print( fg( fmt::color::crimson ) | fmt::emphasis::bold, 
                        "Unable to allocate the metric array.\n" );
            throw std::logic_error( "Unable to set metric size" );
        }
        for ( auto const& welt : M_mesh->elements() )
        {
            auto const& [key, elt] = boost::unwrap_ref( welt );
            for ( auto const& ldof : m.functionSpace()->dof()->localDof( elt.id() ) )
            {
                size_type index = ldof.second.index();
                uint16_type local_dof = ldof.first.localDof();
                auto s = m( index );
                int pos = pt_id[elt.point( local_dof ).id()];
                if constexpr ( dimension_v<MeshType> == 3 )
                {
                    if ( PMMG_Set_scalarMet( mesh, s, pos ) != 1 )
                    {
                        fmt::print( fg( fmt::color::crimson ) | fmt::emphasis::bold,
                                    "Unable to set metric {} at pos {}.\n", s, pos );
                        throw std::logic_error( "Unable to set metric" );
                    }
                }
            }
        }
        double* sol = new double[ m.nLocalDof() ];
        for ( int k = 0; k < m.nLocalDof(); k++ )
        {
            /* Vertex by vertex */
            if ( PMMG_Get_scalarMet( mesh, &sol[k] ) != 1 )
            {
                LOG(ERROR) << fmt::format( fg( fmt::color::crimson ) | fmt::emphasis::bold, 
                                           "Unable to get metrics {} \n", k );
            }
            if( sol[k] <= 0.0 )
            {
                LOG(ERROR) << fmt::format( fg( fmt::color::crimson ) | fmt::emphasis::bold,
                                           "Invalid metrics {} at {} \n", sol[k], k );
            }
        }
        delete[] sol;
    }
}
template <typename MeshType>
typename Remesh<MeshType>::mmg_mesh_t
Remesh<MeshType>::mesh2Mmg( std::shared_ptr<MeshType> const& m_in )
{
    int nVertices = nelements(points(m_in));
    int nTetrahedra = ( dimension_v<MeshType> == 3 ) ? nelements(elements(m_in)) : 0;
    int nPrisms = 0;
    auto boundary_and_marked_faces = concatenate( boundaryfaces( m_in ), markedfaces( m_in, M_required_facet_markers ) );
    int nTriangles = ( dimension_v<MeshType> == 3 ) ? ( (m_in->worldCommPtr()->localSize() > 1)?
        nelements(boundary_and_marked_faces)+nelements(interprocessfaces(m_in)):m_in->numFaces() ) : nelements(elements(m_in));
    int nQuadrilaterals = 0;
    int nEdges = ( dimension_v<MeshType> == 2 ) ? m_in->numFaces() : 0;
    
    if ( std::holds_alternative<MMG5_pMesh>( M_mmg_mesh ) )
    {
        auto mesh = std::get<MMG5_pMesh>( M_mmg_mesh );
        if constexpr ( dimension_v<MeshType> == 3 )
        {
            if ( MMG3D_Set_meshSize( mesh, nVertices, nTetrahedra, nPrisms, nTriangles,
                                     nQuadrilaterals, nEdges ) != 1 )
            {
                throw std::logic_error( "Error in MMG3D_Set_meshSize" );
            }
        }
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
        {
            if ( MMGS_Set_meshSize( mesh, nVertices, nTriangles, nEdges ) != 1 )
            {
                throw std::logic_error( "Error in MMGS_Set_meshSize" );
            }
        }
        else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
        {
            if ( MMG2D_Set_meshSize( mesh, nVertices, nTriangles, nQuadrilaterals, nEdges ) != 1 )
            {
                throw std::logic_error( "Error in MMG2D_Set_meshSize" );
            }
        }

        pt_id.reserve( nVertices );
        int k = 1;
        for ( auto const& wpt : points(m_in) )
        {
            auto const& pt = boost::unwrap_ref( wpt );
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( MMG3D_Set_vertex( std::get<MMG5_pMesh>( M_mmg_mesh ), pt( 0 ), pt( 1 ), pt( 2 ), pt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG3D_Set_vertex" );
                }
                if ( pt.hasMarker() && MMG3D_Set_requiredVertex( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG3D_Set_requiredVertex" );
                }
                pt_id[pt.id()] = k++;
            }
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
            {
                if ( MMGS_Set_vertex( std::get<MMG5_pMesh>( M_mmg_mesh ), pt( 0 ), pt( 1 ), pt( 2 ), pt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMGS_Set_vertex" );
                }
                if ( pt.hasMarker() && MMGS_Set_requiredVertex( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMGS_Set_requiredVertex" );
                }
                pt_id[pt.id()] = k++;
            }
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
            {
                if ( MMG2D_Set_vertex( std::get<MMG5_pMesh>( M_mmg_mesh ), pt( 0 ), pt( 1 ), pt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_vertex" );
                }
                if ( pt.hasMarker() && MMG2D_Set_requiredVertex( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_requiredVertex" );
                }
                pt_id[pt.id()] = k++;
            }
        }
        auto required_element_ids = m_in->markersId( M_required_element_markers );
        k = 1;
        for ( auto const& welt : m_in->elements() )
        {
            auto const& [key, elt] = boost::unwrap_ref( welt );
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( MMG3D_Set_tetrahedron( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                            pt_id[elt.point( 0 ).id()],
                                            pt_id[elt.point( 1 ).id()],
                                            pt_id[elt.point( 2 ).id()],
                                            pt_id[elt.point( 3 ).id()],
                                            elt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG3D_Set_tetrahedron" );
                }
                if ( required_element_ids.count( elt.markerOr( 0 ).value() ) &&
                     MMG3D_Set_requiredTetrahedron( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG3D_Set_requiredTetrahedron" );
                }
            }
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
            {
                if ( MMGS_Set_triangle( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                        pt_id[elt.point( 0 ).id()],
                                        pt_id[elt.point( 1 ).id()],
                                        pt_id[elt.point( 2 ).id()],
                                        elt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMGS_Set_triangle" );
                }
                if ( required_element_ids.count( elt.markerOr( 0 ).value() ) &&
                     MMGS_Set_requiredTriangle( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMGS_Set_requiredTriangle" );
                }
            }
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
            {
                if ( MMG2D_Set_triangle( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                         pt_id[elt.point( 0 ).id()],
                                         pt_id[elt.point( 1 ).id()],
                                         pt_id[elt.point( 2 ).id()],
                                         elt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_triangle" );
                }
                if ( required_element_ids.count( elt.markerOr( 0 ).value() ) &&
                     MMG2D_Set_requiredTriangle( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_requiredTriangle" );
                }
            }
            k++;
        }

        auto required_facet_ids = m_in->markersId( M_required_facet_markers );

        k = 1;
        for ( auto const& wface : m_in->faces() )
        {
            auto const& [key, face] = boost::unwrap_ref( wface );
            if constexpr ( dimension_v<MeshType> == 3 )
            {

                if ( MMG3D_Set_triangle( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                         pt_id[face.point( 0 ).id()],
                                         pt_id[face.point( 1 ).id()],
                                         pt_id[face.point( 2 ).id()],
                                         face.markerOr( 0 ).value(), k ) != 1 )
                {
                    using namespace std::string_literals;
                    throw std::logic_error( "Error in MMG3D_Set_triangle "s + std::to_string( face.id() ) + " " + std::to_string( face.markerOr( 0 ).value() ) );
                }
                if ( required_facet_ids.count( face.markerOr( 0 ).value() ) &&
                     MMG3D_Set_requiredTriangle( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_requiredTriangle" );
                }
                face_id[face.id()] = std::pair{ k, k };
            }
            else if constexpr ( dimension_v<MeshType> == 2 )
            {

                if ( MMG2D_Set_edge( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                     pt_id[face.point( 0 ).id()],
                                     pt_id[face.point( 1 ).id()],
                                     face.markerOr( 0 ).value(), k ) != 1 )
                {
                    using namespace std::string_literals;
                    throw std::logic_error( "Error in MMG2D_Set_edge "s + std::to_string( face.id() ) + " " + std::to_string( face.markerOr( 0 ).value() ) );
                }
                if ( required_facet_ids.count( face.markerOr( 0 ).value() ) &&
                     MMG2D_Set_requiredEdge( std::get<MMG5_pMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in MMG2D_Set_requiredEdge" );
                }
            }

            // update facet index
            k++;
        }
    }
    else if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
    {
        LOG( INFO ) << fmt::format( "nvertices: {}, nTriangles:{} b:{} i:{} t:{}, ntetra: {}", nVertices, nTriangles, nelements(boundaryfaces( M_mesh )),
                                    nelements(interprocessfaces( M_mesh )), nelements(boundaryfaces( M_mesh )) + nelements(interprocessfaces( M_mesh )), nTetrahedra );
        if constexpr ( dimension_v<MeshType> == 3 )
        {
            if ( PMMG_Set_meshSize( std::get<PMMG_pParMesh>( M_mmg_mesh ), nVertices, nTetrahedra, nPrisms, nTriangles,
                                    nQuadrilaterals, nEdges ) != 1 )
            {
                throw std::logic_error( "Error in PMMG_Set_meshSize" );
            }
        }
        pt_id.reserve( nVertices );
        int k = 1;
        for ( auto const& wpt : points(m_in) )
        {
            auto const& pt = boost::unwrap_ref( wpt );
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( PMMG_Set_vertex( std::get<PMMG_pParMesh>( M_mmg_mesh ), pt( 0 ), pt( 1 ), pt( 2 ), pt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in PMMG_Set_vertex" );
                }
                if ( pt.hasMarker() && PMMG_Set_requiredVertex( std::get<PMMG_pParMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in PMMG_Set_requiredVertex" );
                }
                pt_id[pt.id()] = k;
                k++;
            }
        }
        assert( k == nVertices + 1 );
        auto required_element_ids = m_in->markersId( M_required_element_markers );
        k = 1;
        for ( auto const& welt : elements(m_in) )
        {
            auto const& elt = boost::unwrap_ref( welt );
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( PMMG_Set_tetrahedron( std::get<PMMG_pParMesh>( M_mmg_mesh ),
                                           pt_id[elt.point( 0 ).id()],
                                           pt_id[elt.point( 1 ).id()],
                                           pt_id[elt.point( 2 ).id()],
                                           pt_id[elt.point( 3 ).id()],
                                           elt.markerOr( 0 ).value(), k ) != 1 )
                {
                    throw std::logic_error( "Error in PMMG_Set_tetrahedron" );
                }
                if ( required_element_ids.count( elt.markerOr( 0 ).value() ) &&
                     PMMG_Set_requiredTetrahedron( std::get<PMMG_pParMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in PMMG_Set_requiredTetrahedron" );
                }
                k++;
            }
        }
        assert(k == nTetrahedra+1);
        auto required_facet_ids = m_in->markersId( M_required_facet_markers );

        k = 1;
        for ( auto const& wface : boundary_and_marked_faces )
        {
            auto const& face = boost::unwrap_ref( wface );
            if constexpr ( dimension_v<MeshType> == 3 )
            {

                if ( PMMG_Set_triangle( std::get<PMMG_pParMesh>( M_mmg_mesh ),
                                        pt_id[face.point( 0 ).id()],
                                        pt_id[face.point( 1 ).id()],
                                        pt_id[face.point( 2 ).id()],
                                        face.markerOr( 0 ).value(), k ) != 1 )
                {
                    using namespace std::string_literals;
                    throw std::logic_error( "Error in PMMG_Set_triangle "s + std::to_string( face.id() ) + " " + std::to_string( face.markerOr( 0 ).value() ) );
                }
                if ( required_facet_ids.count( face.markerOr( 0 ).value() ) &&
                     PMMG_Set_requiredTriangle( std::get<PMMG_pParMesh>( M_mmg_mesh ), k ) != 1 )
                {
                    throw std::logic_error( "Error in PMMG_Set_requiredTriangle" );
                }

                auto s = cantor_pairing( face.point( 0 ).id(), face.point( 1 ).id(), face.point( 2 ).id() );
                DVLOG(3) << fmt::format( "face: {} pt: {} {} {} renumber: {} pt: {} {} {}",
                                          face.id(), face.point( 0 ).id(), face.point( 1 ).id(), face.point( 2 ).id(),
                                          k, pt_id[face.point( 0 ).id()],pt_id[face.point( 1 ).id()],pt_id[face.point( 2 ).id()] );
                face_id[face.id()] = std::make_pair( k, s );
                k++;
            }
        }
        std::vector<int> pointIdInElt( 4 ), pointIdInFace( 3 );
        for( auto const& wface : interprocessfaces(M_mesh))
        {
            auto const& face = boost::unwrap_ref( wface );
            auto const& elt0 = face.element0();
            int idInElt0 = face.idInElement0();
            auto const& elt1 = face.element1();
            int idInElt1 = face.idInElement1();
            const bool elt0isGhost = elt0.isGhostCell();
            auto const& elt = ( elt0isGhost ) ? elt1 : elt0;
            int faceIdInElt = ( elt0isGhost ) ? idInElt1 : idInElt0;
            for ( uint16_type f = 0; f < elt.numVertices; ++f )
                pointIdInElt[f] = elt.point( f ).id();
            for ( uint16_type f = 0; f < 3; ++f )
            {
                pointIdInFace[f] = pointIdInElt[elt.fToP( faceIdInElt, f )];
            }
            DVLOG(3) << fmt::format( "interprocess face: {} {} {}, vids:{}, {} {} {}", face.id(), faceIdInElt, k, pointIdInFace,
                                     pt_id[pointIdInFace[0]], pt_id[pointIdInFace[1]], pt_id[pointIdInFace[2]] );
            if ( PMMG_Set_triangle( std::get<PMMG_pParMesh>( M_mmg_mesh ),
                                    pt_id[pointIdInFace[0]],
                                    pt_id[pointIdInFace[1]],
                                    pt_id[pointIdInFace[2]],
                                    face.markerOr( 0 ).value(), k ) != 1 )
            {
                using namespace std::string_literals;
                throw std::logic_error( "Error in PMMG_Set_triangle "s + std::to_string( face.id() ) + " " + std::to_string( face.markerOr( 0 ).value() ) );
            }
            face_id[face.id()] = std::make_pair( k, 0 );
            k++;
        }
        assert( k == nTriangles + 1 );
    }
    return M_mmg_mesh;
}

template <typename MeshType>
std::shared_ptr<MeshType>
Remesh<MeshType>::mmg2Mesh( mmg_mesh_t const& mesh )
{
    int ier;

    int nVertices = 0;
    int nTetrahedra = 0;
    int nTriangles = 0;
    int nEdges = 0;

    std::shared_ptr<MeshType> out = std::make_shared<MeshType>();

    if ( std::holds_alternative<MMG5_pMesh>( mesh ) )
    {
        if ( MMG3D_Get_meshSize( std::get<MMG5_pMesh>( mesh ), &nVertices, &nTetrahedra, NULL, &nTriangles, NULL,
                                 &nEdges ) != 1 )
        {
            ier = MMG5_STRONGFAILURE;
        }
        int corner, required, tag = 0;
        node_type n( mesh_t::nRealDim );
        for ( int k = 1; k <= nVertices; k++ )
        {
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( MMG3D_Get_vertex( std::get<MMG5_pMesh>( mesh ), &( n[0] ), &( n[1] ), &( n[2] ),
                                       &( tag ), &( corner ), &( required ) ) != 1 )
                {
                    cout << "Unable to get mesh vertex " << k << endl;
                    ier = MMG5_STRONGFAILURE;
                }
            }
            else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3 )
            {
                if ( MMGS_Get_vertex( std::get<MMG5_pMesh>( mesh ), &( n[0] ), &( n[1] ), &( n[2] ),
                                      &( tag ), &( corner ), &( required ) ) != 1 )
                {
                    cout << "Unable to get mesh vertex " << k << endl;
                    ier = MMG5_STRONGFAILURE;
                }
            }
            if constexpr ( dimension_v<MeshType> == 2 )
            {

                if ( MMG2D_Get_vertex( std::get<MMG5_pMesh>( mesh ), &( n[0] ), &( n[1] ),
                                       &( tag ), &( corner ), &( required ) ) != 1 )
                {
                    cout << "Unable to get mesh 2D vertex " << k << endl;
                    ier = MMG5_STRONGFAILURE;
                }
            }
            using point_type = typename mesh_t::point_type;
            point_type pt( k, n );
            pt.setProcessIdInPartition( 0 );
            pt.setProcessId( 0 );
            pt.setMarker( tag );
            out->addPoint( pt );
        }

        if constexpr ( dimension_v<MeshType> == 3 )
        {
            for ( int k = 1; k <= nTetrahedra; k++ )
            {
                int iv[4], lab;
                if ( MMG3D_Get_tetrahedron( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                            &( iv[0] ), &( iv[1] ),
                                            &( iv[2] ), &( iv[3] ),
                                            &( lab ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh tetra " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }

                using element_type = typename mesh_t::element_type;
                element_type newElem;
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( 0 );
                newElem.setProcessId( 0 );
                for ( int i = 0; i < 4; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                out->addElement( newElem, true );
            }
        }
        for ( int k = 1; k <= nTriangles; k++ )
        {
            int iv[3], lab;
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( MMG3D_Get_triangle( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                         &( iv[0] ), &( iv[1] ), &( iv[2] ),
                                         &( lab ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh triangle " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }
                using face_type = typename mesh_t::face_type;
                face_type newElem;
                newElem.setId( k );
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( 0 );
                newElem.setProcessId( 0 );
                for ( int i = 0; i < 3; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                out->addFace( newElem );
            }
            if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2 )
            {
                if ( MMG2D_Get_triangle( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                         &( iv[0] ), &( iv[1] ), &( iv[2] ),
                                         &( lab ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh triangle " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }
                using element_type = typename mesh_t::element_type;
                element_type newElem;
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( 0 );
                newElem.setProcessId( 0 );
                for ( int i = 0; i < 3; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                out->addElement( newElem, true );
            }
        }
        for ( int k = 1; k <= nEdges; k++ )
        {
            int iv[2], lab, isridge = 0;
            if constexpr ( dimension_v<MeshType> == 2 )
            {
                if ( MMG2D_Get_edge( std::get<MMG5_pMesh>( M_mmg_mesh ),
                                     &( iv[0] ), &( iv[1] ),
                                     &( lab ), &( isridge ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh triangle " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }
                using face_type = typename mesh_t::face_type;
                face_type newElem;
                newElem.setId( k );
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( 0 );
                newElem.setProcessId( 0 );
                for ( int i = 0; i < 2; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                auto [it, ins] = out->addFace( newElem );
            }
        }
        out->setMarkerNames( M_mesh->markerNames() );
        out->updateForUse();
    }
    if ( std::holds_alternative<PMMG_pParMesh>( mesh ) )
    {
        if ( PMMG_Get_meshSize( std::get<PMMG_pParMesh>( mesh ), &nVertices, &nTetrahedra, NULL, &nTriangles, NULL,
                                &nEdges ) != 1 )
        {
            ier = MMG5_STRONGFAILURE;
        }
        int corner, required, tag = 0;
        node_type n( mesh_t::nRealDim );
        for ( int k = 1; k <= nVertices; k++ )
        {
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( PMMG_Get_vertex( std::get<PMMG_pParMesh>( mesh ), &( n[0] ), &( n[1] ), &( n[2] ),
                                      &( tag ), &( corner ), &( required ) ) != 1 )
                {
                    cout << "Unable to get mesh vertex " << k << endl;
                    ier = MMG5_STRONGFAILURE;
                }
            }
            using point_type = typename mesh_t::point_type;
            point_type pt( k, n );
            pt.setProcessIdInPartition( out->worldComm().localRank() );
            pt.setProcessId( out->worldComm().localRank() );
            pt.setMarker( tag );
            out->addPoint( std::move(pt) );
        }

        if constexpr ( dimension_v<MeshType> == 3 )
        {
            for ( int k = 1; k <= nTetrahedra; k++ )
            {
                int iv[4], lab;
                if ( PMMG_Get_tetrahedron( std::get<PMMG_pParMesh>( M_mmg_mesh ),
                                           &( iv[0] ), &( iv[1] ),
                                           &( iv[2] ), &( iv[3] ),
                                           &( lab ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh tetra " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }

                using element_type = typename mesh_t::element_type;
                element_type newElem;
                newElem.setId( k );
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( out->worldComm().localRank() );
                newElem.setProcessId( out->worldComm().localRank() );
                for ( int i = 0; i < 4; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                out->addElement( std::move(newElem) );
            }
        }
        auto [neigh_interface, local_faces, face_to_interface] = getCommunicatorAPI();
        for ( int k = 1; k <= nTriangles; k++ )
        {
            int iv[3], lab;
            if constexpr ( dimension_v<MeshType> == 3 )
            {
                if ( PMMG_Get_triangle( std::get<PMMG_pParMesh>( M_mmg_mesh ),
                                        &( iv[0] ), &( iv[1] ), &( iv[2] ),
                                        &( lab ), &( required ) ) != 1 )
                {
                    std::cout << "Unable to get mesh triangle " << k << std::endl;
                    ier = MMG5_STRONGFAILURE;
                }
                using face_type = typename mesh_t::face_type;
                face_type newElem;
                newElem.setId( k );
                
                if ( auto fit = face_to_interface.find(k); fit != face_to_interface.end() )
                {
                    LOG(INFO) << fmt::format("interface {} face {} with pid {}", fit->second, k, neigh_interface[fit->second].first );
                    lab = 1234567;
                }
                newElem.setMarker( lab );
                newElem.setProcessIdInPartition( out->worldComm().localRank() );
                newElem.setProcessId( out->worldComm().localRank() );
                for ( int i = 0; i < 3; i++ )
                    newElem.setPoint( i, out->point( iv[i] ) );
                out->addFace( std::move(newElem) );
            }
        }
        out->setMarkerNames( M_mesh->markerNames() );
        out->updateForUse();
#if 0        
        int current_pid = M_mesh->worldComm().localRank();
        int interf = 0;
        std::vector<mesh_ptr_t> ghost_out( neigh_interface.size() );
        std::vector<mesh_ptr_t> ghost_in( neigh_interface.size() );
        std::vector<mpi::request> reqs( 2*neigh_interface.size() );
        for ( auto [pid, sz] : neigh_interface )
        {
            ext_elements_t<mesh_t> range_ghost;
            range_element_ptr_t<mesh_t> r( new range_element_t<mesh_t>() );
#if 0            
            // now extract elements shared elements
            for ( auto const& welt : elements( out ) )
            {
                auto const& elt = boost::unwrap_ref( welt );
                if ( elt.hasFaceWithMarker( 1234567 ) )
                    r->push_back( boost::cref( elt ) );
            }
#else
            auto rfaces = markedfaces(out, 1234567 );
            LOG(INFO) << fmt::format("interface with {} nfaces interface {} nfaces marked {} ", 
                                     pid, sz, nelements(rfaces));
            for( auto const& wf : rfaces )
            {
                auto const& f = boost::unwrap_ref( wf );
                CHECK( f.isConnectedTo0() ) << f;
                r->push_back( boost::cref( f.element0() ) );
            }
#endif
            range_ghost = boost::make_tuple( mpl::size_t<MESH_ELEMENTS>(),
                                             r->begin(),
                                             r->end(),
                                             r );
            ghost_out[interf] = createSubmesh( out, range_ghost );
            // send mesh to pid and get back the ghosts from the other side
            ghost_in[interf] = std::make_shared<mesh_t>();
            int  tag_send = (current_pid < pid)?0:1;
            int  tag_recv = (current_pid < pid)?1:0;

            reqs[2 * interf] = out->worldCommPtr()->localComm().isend( pid, tag_send, *ghost_out[interf] );
            reqs[2 * interf + 1] = out->worldCommPtr()->localComm().irecv( pid, tag_recv, *ghost_in[interf] );
            interf++;
        }
        mpi::wait_all( reqs.data(), reqs.data() + 2 * neigh_interface.size() );
#endif        
    }
    return out;
}

template <typename MeshType>
void Remesh<MeshType>::setParameters()
{
#if 0
    if constexpr ( dimension_v<MeshType> == 3 )
    {
        MMG3D_Set_iparameter(M_mmg_mesh,M_mmg_sol,MMG3D_IPARAM_verbose, value<MmgOption::Verbose>());
        MMG3D_Set_iparameter(M_mmg_mesh,M_mmg_sol,MMG3D_IPARAM_mem, value<MmgOption::Mem>());
        MMG3D_Set_dparameter(M_mmg_mesh,M_mmg_sol,MMG3D_DPARAM_hmin, value<MmgOption::Hmin>());
        MMG3D_Set_dparameter(M_mmg_mesh,M_mmg_sol,MMG3D_DPARAM_hmax, value<MmgOption::Hmax>());
        MMG3D_Set_dparameter(M_mmg_mesh,M_mmg_sol,MMG3D_DPARAM_hsiz, value<MmgOption::Hsiz>());
    }
    else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 3  )
    {
    }
    else if constexpr ( dimension_v<MeshType> == 2 && real_dimension_v<MeshType> == 2  )
    {

    }
#endif
    if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
    {
#if 0        
        /* Set number of iterations */
        if ( !PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_niter, 1 ) )
        {
            throw std::logic_error( "Unable to set the number of iteration of the adaptation process." );
        }
#endif
        /* Don't remesh the surface */
        if ( !PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_nosurf, 1 ) )
        {
            throw std::logic_error( "Unable to disable surface adaptation." );
        }

        /* Compute output nodes and triangles global numbering */
        if ( !PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_globalNum, 1 ) )
        {
            throw std::logic_error( "Unable to compute output globa ids for nodes and triangles." );
        }
    }
}

template <typename MeshType>
void Remesh<MeshType>::setCommunicatorAPI()
{
    if ( std::holds_alternative<PMMG_pParMesh>( M_mmg_mesh ) )
    {
        if ( !PMMG_Set_iparameter( std::get<PMMG_pParMesh>( M_mmg_mesh ), PMMG_IPARAM_APImode, PMMG_APIDISTRIB_faces ) )
        {
            throw std::logic_error( "Unable to set the faces API mode." );
        }
        auto [ifaces_beg, ifaces_end, ifaces] = M_mesh->interProcessFaces();
        std::map<int, int> neighbor_ent;
        std::map<std::pair<int,int>, int> ids_other;
        std::vector<int> loc;
        for ( auto it = ifaces_beg; it != ifaces_end; ++it )
        {
            auto const& faceip = boost::unwrap_ref( *it );
            auto const& elt0 = faceip.element0();
            auto const& elt1 = faceip.element1();
            const bool elt0isGhost = elt0.isGhostCell();
            auto const& eltOffProc = ( elt0isGhost ) ? elt0 : elt1;

            if ( !neighbor_ent.count( eltOffProc.processId() ) )
                neighbor_ent[eltOffProc.processId()] = 0;
            ++neighbor_ent[eltOffProc.processId()];
            ids_other[std::pair{ faceip.id(), eltOffProc.processId() }] = faceip.idInOthersPartitions( eltOffProc.processId() );
        }
        /* Set the number of interfaces */
        if ( !PMMG_Set_numberOfFaceCommunicators( std::get<PMMG_pParMesh>( M_mmg_mesh ), neighbor_ent.size() ) )
        {
            throw std::logic_error( "Unable to set the number of local parallel faces." );
        }
        LOG(INFO) << "number of interfaces: " << neighbor_ent.size() << "\n";
        int lcomm = 0;
        int current_pid = M_mesh->worldComm().localRank();
        for ( auto [pid, sz] : neighbor_ent )
        {
            LOG( INFO ) << "setting ParMmg communicator with " << pid << " (" << sz << " shared faces)" << std::endl;
            /* Set nb. of entities on interface and rank of the outward proc */
            if ( !PMMG_Set_ithFaceCommunicatorSize( std::get<PMMG_pParMesh>( M_mmg_mesh ), lcomm, pid, sz ) )
            {
                throw std::logic_error( "Unable to set internode data." );
            }
            /* Set local and global index for each entity on the interface */
            std::vector<int> local( sz );
            std::vector<std::pair<int,int>> local_sorted( sz );
            std::vector<int> local_wo_ren( sz );
            std::vector<int> global( sz, 0 );
            std::vector<int> other( sz );
            int fid = 0;
            for ( auto const& wf : interprocessfaces( M_mesh, pid ) )
            {
                auto const& f = boost::unwrap_ref( wf );
                // if current  pid is greater than pid then sort the local faces with respect to the id
                // in the partition pid so that the faces are set in the same order
                if ( current_pid < pid )
                    local_sorted[fid] = { f.id(), ids_other[std::pair{ f.id(), pid }] };
                else
                    local_sorted[fid] = { ids_other[std::pair{ f.id(), pid }], f.id() };
#if 0
                local[fid] = face_id[f.id()].first;
               
                global[fid] = 0;//face_id[f.id()].second;
                
#endif
                local_wo_ren[fid] = f.id();
                other[fid] = ids_other[std::pair{ f.id(), pid }];
                ++fid;
            }
            std::sort( local_sorted.begin(), local_sorted.end() );
            int i = 0;
            for( auto [id1, id2]: local_sorted )
            {
                int id = ( current_pid < pid )?id1:id2;
                local[i] = face_id[id].first;
                ++i;
            }
            // we have sorted the faces so that we don't have to provide a global index
            if ( !PMMG_Set_ithFaceCommunicator_faces( std::get<PMMG_pParMesh>( M_mmg_mesh ), lcomm,
                                                      local.data(),
                                                      global.data(), 
                                                      0 ) ) 
            {
                throw std::logic_error( "Unable to set interprocess face data." );
            }
            ++lcomm;
        }
    }
}
template <typename MeshType>
typename Remesh<MeshType>::comm_t
Remesh<MeshType>::getCommunicatorAPI()
{
    LOG(INFO) << "getCommunicatorAPI - set interface data with neighbors...";
    int n_neigh;
    if ( !PMMG_Get_numberOfFaceCommunicators( std::get<PMMG_pParMesh>( M_mmg_mesh ), &n_neigh ) )
    {
        throw std::logic_error( "Unable to get the number of local parallel faces." );
    }
    //using neighbor_ent_t = std::map<int, int>;
    //using local_face_index_t = std::vector<std::vector<int>>;
    //using comm_t = std::tuple<neighbor_ent_t, local_face_index_t>;
    neighbor_ent_t neighbor_ent;
    
    int** local_faces = new int*[n_neigh];
    for ( int s = 0; s < n_neigh; ++ s )
    {
        int pid, sz;
        /* Set nb. of entities on interface and rank of the outward proc */
        if ( !PMMG_Get_ithFaceCommunicatorSize( std::get<PMMG_pParMesh>( M_mmg_mesh ), s, &pid, &sz ) )
        {
            throw std::logic_error( "Unable to get interface data." );
        }
        LOG( INFO ) << fmt::format( "setting ParMmg communicator {} with {} and {} shared faces)", s, pid, sz );
        neighbor_ent[s]=std::pair{pid,sz};
        local_faces[s]=new int[sz];
    }

    if ( !PMMG_Get_FaceCommunicator_faces( std::get<PMMG_pParMesh>( M_mmg_mesh ), local_faces ) )
    {
        throw std::logic_error( "Unable to get interface faces data." );
    }

    std::map<int,int> face_to_interface;
    local_face_index_t local_face_index( n_neigh );
    for ( int s = 0; s < n_neigh; ++s )
    {
        local_face_index[s].resize( neighbor_ent[s].second );
        std::copy( local_faces[s], local_faces[s] + neighbor_ent[s].second, local_face_index[s].begin() );
        for(int f: local_face_index[s] )
        {
            face_to_interface[f] = s;
        }
        LOG(INFO) << fmt::format("interface {} local face with pid {} : {}", s, neighbor_ent[s].first, local_face_index );
        delete [] local_faces[s];
    }
    delete [] local_faces;
    return  std::tuple{ neighbor_ent, local_face_index, face_to_interface };
}
} // namespace Feel
