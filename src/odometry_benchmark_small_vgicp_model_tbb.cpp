#include <small_gicp/benchmark/benchmark_odom.hpp>

#include <tbb/tbb.h>
#include <small_gicp/factors/gicp_factor.hpp>
#include <small_gicp/points/point_cloud.hpp>
#include <small_gicp/ann/gaussian_voxelmap.hpp>
#include <small_gicp/util/normal_estimation_tbb.hpp>
#include <small_gicp/registration/reduction_tbb.hpp>
#include <small_gicp/registration/registration.hpp>

#include <guik/viewer/async_light_viewer.hpp>

namespace small_gicp {

class SmallVGICPModelOnlineOdometryEstimationTBB : public OnlineOdometryEstimation {
public:
  SmallVGICPModelOnlineOdometryEstimationTBB(const OdometryEstimationParams& params)
  : OnlineOdometryEstimation(params),
    control(tbb::global_control::max_allowed_parallelism, params.num_threads),
    T(Eigen::Isometry3d::Identity()) {
    sub_viewer = guik::async_viewer()->async_sub_viewer("model points");
  }

  Eigen::Isometry3d estimate(const PointCloud::Ptr& points) override {
    Stopwatch sw;
    sw.start();

    estimate_covariances_tbb(*points, 10);

    if (voxelmap == nullptr) {
      voxelmap = std::make_shared<GaussianVoxelMap>(params.voxel_resolution);
      voxelmap->insert(*points);
      return T;
    }

    Registration<GICPFactor, ParallelReductionTBB, DistanceRejector, LevenbergMarquardtOptimizer> registration;
    auto result = registration.align(*voxelmap, *points, *voxelmap, T);

    T = result.T_target_source;
    voxelmap->insert(*points, T);

    sub_viewer.update_points("current", points->points, guik::FlatOrange(T).set_point_scale(2.0f));
    sub_viewer.update_normal_dists("voxelmap", voxelmap->voxel_means(), voxelmap->voxel_covs(), 1.0, guik::Rainbow());
    sub_viewer.lookat(T.translation().cast<float>());

    sw.stop();
    reg_times.push(sw.msec());

    return T;
  }

  void report() override {  //
    std::cout << "registration_time_stats=" << reg_times.str() << " [msec/scan]  total_throughput=" << total_times.str() << " [msec/scan]" << std::endl;
  }

private:
  guik::AsyncLightViewerContext sub_viewer;

  tbb::global_control control;

  Summarizer reg_times;

  GaussianVoxelMap::Ptr voxelmap;
  Eigen::Isometry3d T;
};

static auto small_gicp_model_tbb_registry =
  register_odometry("small_vgicp_model_tbb", [](const OdometryEstimationParams& params) { return std::make_shared<SmallVGICPModelOnlineOdometryEstimationTBB>(params); });

}  // namespace small_gicp