#include <feel/feelcrb/toolboxmor.hpp>
#include <feel/feelmodels/heat/heat.hpp>

using namespace Feel;

void writeErrors(fs::ofstream& out, std::vector<std::vector<double> > const& err)
{
    if( out && Environment::isMasterRank() )
    {
        int N = err.size();
        int size = err[0].size();
        out << std::setw(5) << "N";
        for(int i = 0; i < size; ++i)
            out << std::setw(24) << "mu_" << i;
        out << std::endl;
        for(int n = 0; n < N; ++n)
        {
            out << std::setw(5) << n+1;
            for(int i = 0; i < size; ++i)
                out << std::setw(25) << err[n][i];
            out << std::endl;
        }
        out.close();
    }
}

int main( int argc, char** argv)
{
    po::options_description opt("options");
    Environment env( _argc=argc, _argv=argv,
                     _desc=opt.add(makeToolboxMorOptions())
                     .add(toolboxes_options("heat")) );

    using heat_tb_type = FeelModels::Heat<Simplex<2>, Lagrange<1, Scalar, Continuous, PointSetFekete> >;
    using heat_tb_ptrtype = std::shared_ptr<heat_tb_type>;
    using space_type = typename heat_tb_type::space_temperature_type;

    using rb_model_type = ToolboxMor<2>;
    using rb_model_ptrtype = std::shared_ptr<rb_model_type>;
    using crb_model_type = CRBModel<rb_model_type>;
    using crb_model_ptrtype = std::shared_ptr<crb_model_type>;
    using crb_type = CRB<crb_model_type>;
    using crb_ptrtype = std::shared_ptr<crb_type>;
    using wn_type = typename crb_type::wn_type;
    using vectorN_type = Eigen::VectorXd;
    using export_vector_wn_type = typename crb_type::export_vector_wn_type;
    using mesh_type = rb_model_type::mesh_type;
    using mesh_ptrtype = rb_model_type::mesh_ptrtype;
    using parameter_type = typename rb_model_type::parameter_type;
    using sampling_type = typename crb_type::sampling_type;
    using sampling_ptrtype = std::shared_ptr<sampling_type>;

    using deim_function_type = typename rb_model_type::deim_function_type;
    using mdeim_function_type = typename rb_model_type::mdeim_function_type;

    auto heatBox = heat_tb_type::New(_prefix="heat");
    heatBox->init();
    heatBox->printAndSaveInfo();
    rb_model_ptrtype model = std::make_shared<rb_model_type>();
    model->setFunctionSpaces(heatBox->spaceTemperature());
    auto rhs = heatBox->algebraicFactory()->rhs()->clone();
    auto mat = heatBox->algebraicFactory()->matrix();
    deim_function_type assembleDEIM =
        [&heatBox,&rhs,&mat](parameter_type const& mu)
            {
                for( int i = 0; i < mu.size(); ++i )
                    heatBox->addParameterInModelProperties(mu.parameterName(i), mu(i));
                heatBox->updateParameterValues();
                rhs->zero();
                heatBox->algebraicFactory()->applyAssemblyLinear( heatBox->blockVectorSolution().vectorMonolithic(), mat, rhs, {"ignore-assembly.lhs"} );
                return rhs;
            };
    model->setAssembleDEIM(assembleDEIM);
    mdeim_function_type assembleMDEIM =
        [&heatBox,&rhs,&mat](parameter_type const& mu)
            {
                for( int i = 0; i < mu.size(); ++i )
                    heatBox->addParameterInModelProperties(mu.parameterName(i), mu(i));
                heatBox->updateParameterValues();
                mat->zero();
                heatBox->algebraicFactory()->applyAssemblyLinear( heatBox->blockVectorSolution().vectorMonolithic(), mat, rhs, {"ignore-assembly.rhs"} );
                return mat;
            };
    model->setAssembleMDEIM(assembleMDEIM);
    model->initModel();

    auto deimHeatBox = std::make_shared<heat_tb_type>("heat");
    deimHeatBox->setMesh(model->getDEIMReducedMesh());
    deimHeatBox->init();
    deimHeatBox->printAndSaveInfo();
    deim_function_type assembleOnlineDEIM =
        [deimHeatBox](parameter_type const& mu)
            {
                for( int i = 0; i < mu.size(); ++i )
                    deimHeatBox->addParameterInModelProperties(mu.parameterName(i), mu(i));
                deimHeatBox->updateParameterValues();
                auto rhs = deimHeatBox->algebraicFactory()->rhs()->clone();
                rhs->zero();
                auto matTMP = deimHeatBox->algebraicFactory()->matrix()->clone();
                deimHeatBox->algebraicFactory()->applyAssemblyLinear( deimHeatBox->blockVectorSolution().vectorMonolithic(), matTMP, rhs, {"ignore-assembly.lhs"} );
                return rhs;
            };
    model->setOnlineAssembleDEIM(assembleOnlineDEIM);
    // model->setOnlineAssembleDEIM(assembleDEIM);

    auto mdeimHeatBox = std::make_shared<heat_tb_type>("heat");
    mdeimHeatBox->setMesh(model->getMDEIMReducedMesh());
    mdeimHeatBox->init();
    mdeimHeatBox->printAndSaveInfo();
    mdeim_function_type assembleOnlineMDEIM =
        [mdeimHeatBox](parameter_type const& mu)
            {
                for( int i = 0; i < mu.size(); ++i )
                    mdeimHeatBox->addParameterInModelProperties(mu.parameterName(i), mu(i));
                mdeimHeatBox->updateParameterValues();
                auto mat = mdeimHeatBox->algebraicFactory()->matrix()->clone();
                mat->zero();
                auto rhsTMP = mdeimHeatBox->algebraicFactory()->rhs()->clone();
                mdeimHeatBox->algebraicFactory()->applyAssemblyLinear( mdeimHeatBox->blockVectorSolution().vectorMonolithic(), mat, rhsTMP, {"ignore-assembly.rhs"} );
                return mat;
            };
    model->setOnlineAssembleMDEIM(assembleOnlineMDEIM);
    // model->setOnlineAssembleMDEIM(assembleMDEIM);

    model->postInitModel();
    model->setInitialized(true);

    crb_model_ptrtype crbModel = std::make_shared<crb_model_type>(model);
    crb_ptrtype crb = crb_type::New("toolboxmor", crbModel, crb::stage::offline);

    tic();
    crb->offline();
    toc("offline");

    int N = crb->dimension();
    int timeSteps = 1;
    std::vector<vectorN_type> uNs(timeSteps, vectorN_type(N)), uNolds(timeSteps, vectorN_type(N));
    std::vector<double> outputs(timeSteps, 0);

    sampling_ptrtype sampling( new sampling_type( crbModel->parameterSpace() ) );
    int size = ioption("toolboxmor.sampling-size");
    sampling->clear();
    sampling->randomize( size, true );

    std::vector<std::vector<double> > errs(N, std::vector<double>(size));
    std::vector<std::vector<double> > errsRel(N, std::vector<double>(size));
    auto Xh = model->functionSpace();
    auto mesh = Xh->mesh();
    auto rangeT = elements(mesh);//Xh->dof()->meshSupport()->rangeElements();
    auto TFE = Xh->element();
    auto TRB = Xh->element();

    int j = 0;
    for( auto const& mu : *sampling )
    {
        for( int i = 0; i < mu.size(); ++i )
            heatBox->addParameterInModelProperties(mu.parameterName(i), mu(i));
        heatBox->updateParameterValues();
        // heatBox->updateFieldVelocityConvection();
        heatBox->solve();
        TFE = heatBox->fieldTemperature();
        auto normT = normL2( rangeT, idv(TFE) );
        for(int n = 0; n < N; ++n)
        {
            crb->fixedPointPrimal(n+1, mu, uNs, uNolds, outputs);
            vectorN_type uN = uNs[0];
            TRB = crb->expansion( uN, n+1 );
            errs[n][j] = normL2( rangeT, idv(TRB)-idv(TFE) );
            errsRel[n][j] = errs[n][j]/normT;
        }
        ++j;
    }

    std::vector<double> min(N), max(N), mean(N), stdev(N);
    for(int n = 0; n < N; ++n)
    {
        min[n] = *std::min_element(errsRel[n].begin(), errsRel[n].end());
        max[n] = *std::max_element(errsRel[n].begin(), errsRel[n].end());
        double s = std::accumulate(errsRel[n].begin(), errsRel[n].end(), 0.0);
        mean[n] = s/size;
        double accum = std::accumulate(errsRel[n].begin(), errsRel[n].end(), 0.0,
                                       [s,size](double a, double b) {
                                           return a + (b-s/size)*(b-s/size);
                                       });
        stdev[n] = accum/size;
    }

    fs::ofstream cvgErr( "err.dat" );
    fs::ofstream cvgErrR( "errR.dat" );
    fs::ofstream cvgStat( "stat.dat" );
    writeErrors(cvgErr, errs);
    writeErrors(cvgErrR, errsRel);
    if( cvgStat && Environment::isMasterRank() )
    {
        cvgStat << std::setw(5) << "N" << std::setw(25) << "min" << std::setw(25) << "max"
                << std::setw(25) << "mean" << std::setw(25) << "stdev" << std::endl;
        for(int n = 0; n < N; ++n)
            cvgStat << std::setw(5) << n+1 << std::setw(25) << min[n] << std::setw(25) << max[n]
                    << std::setw(25) << mean[n] << std::setw(25) << stdev[n] << std::endl;
        cvgStat.close();
    }
    Feel::cout << std::setw(5) << "N" << std::setw(25) << "min" << std::setw(25) << "max"
               << std::setw(25) << "mean" << std::setw(25) << "stdev" << std::endl;
    for(int n = 0; n < N; ++n)
        Feel::cout << std::setw(5) << n+1 << std::setw(25) << min[n] << std::setw(25) << max[n]
                   << std::setw(25) << mean[n] << std::setw(25) << stdev[n] << std::endl;

    auto e = exporter(mesh);
    e->add("TFE", TFE);
    e->add("TRB", TRB);
    e->save();

    return 0;
}
