// Copyright 2023 MRC Laboratory of Molecular Biology
//

#ifndef GEMMI_REFINE_LL_HPP_
#define GEMMI_REFINE_LL_HPP_

#include <vector>
#include <gemmi/grid.hpp>
#include <gemmi/it92.hpp>
#include <gemmi/dencalc.hpp>

namespace gemmi {

// from Refmac subroutine SMOOTH_GAUSS_D in extra_eigen.f
std::pair<double, std::vector<double>> smooth_gauss_d(double kernel_width,
                                                      const std::vector<double> &x_points,
                                                      const std::vector<double> &y_points,
                                                      double x_current) {
  assert(x_points.size() == y_points.size());
  const int n_points = x_points.size();
  if (n_points == 1)
    return std::make_pair(y_points[0], std::vector<double>(1, 1.));

  const double kernel_width2 = kernel_width * kernel_width * 2.0;

  // return values
  double y_current = 0;
  std::vector<double> y_derivs(n_points);

  if (x_current <= x_points.back() && x_current >= x_points.front()) {
    double an = 0.0;
    double fn = 0.0;
    double dx = 0, dx0 = 1.0, dx1 = 0;
    for (int i = 0; i < n_points; ++i) {
      dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2;
      dx0 = std::min(std::abs(dx), dx0);
    }
    for (int i = 0; i < n_points; ++i) {
      dx = (x_current - x_points[i]) * (x_current - x_points[i]) / kernel_width2;
      dx1 = dx - dx0;
      if (dx1 <= 120.0) {
        const double expdx = std::exp(-dx1);
        an += expdx;
        fn += y_points[i] * expdx;
        y_derivs[i] = expdx;
      }
    }
    if (an <= 0.0)
      fail("===> Error in smooth gauss. Width might be too small: 1",
                  std::to_string(n_points), " ", std::to_string(kernel_width), " ",
                  std::to_string(dx), " ", std::to_string(dx0), " ", std::to_string(dx1));

    y_current = fn / an;

    // calculate derivatives
    for (int i = 0; i < n_points; ++i) {
      const double dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2;
      const double dx1 = dx - dx0;
      if (dx1 <= 120.0)
        y_derivs[i] /= an;
      else
        y_derivs[i] = 0.0;
    }
  } else if (x_current > x_points.back()) {
    const double dx1 = (x_current - x_points.back())*(x_current - x_points.back()) / kernel_width2;
    double an = 1.0;
    double fn = y_points.back();
    double dx;
    y_derivs.back() = 1.0;
    for (int i = 0; i < n_points-1; ++i) {
      dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2 - dx1;
      if (dx <= 120.0) {
        const double expdx = std::exp(-dx);
        an += expdx;
        fn += y_points[i] * expdx;
        y_derivs[i] = expdx;
      }
    }
    if (an <= 0.0)
      fail("===> Error in smooth gauss. Width might be too small: 2 ",
                  std::to_string(n_points), " ", std::to_string(kernel_width), " ",
                  std::to_string(dx));

    y_current = fn / an;

    // calculate derivatives
    for (int i = 0; i < n_points; ++i) {
      const double dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2 - dx1;
      if (dx <= 120.0)
        y_derivs[i] /= an;
      else
        y_derivs[i] = 0.0;
    }
  } else if (x_current < x_points.front()) {
    const double dx1 = (x_current-x_points.front())*(x_current-x_points.front()) / kernel_width2;
    double an = 1.0;
    double fn = y_points.front();
    double dx;
    y_derivs[0] = 1.0;
    for (int i = 1; i < n_points; ++i) {
      dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2 - dx1;
      if (dx <= 120.0) {
        const double expdx = std::exp(-dx);
        an += expdx;
        fn = y_points[i]*expdx;
        y_derivs[i] = expdx;
      }
    }
    if (an <= 0.0)
      fail("===> Error in smooth gauss. Width might be too small: 3 ",
                  std::to_string(n_points), " ", std::to_string(kernel_width), " ",
                  std::to_string(dx));

    y_current = fn/an;

    // calculate derivatives
    for (int i = 0; i < n_points; ++i) {
      const double dx = (x_current - x_points[i])*(x_current - x_points[i]) / kernel_width2 - dx1;
      if (dx <= 120.0)
        y_derivs[i] /= an;
      else
        y_derivs[i] = 0.0;
    }
  }
  return std::make_pair(y_current, y_derivs);
}

struct TableS3 {
  double s_min, s_max;
  double delta_s3;
  int n_points;
  std::vector<double> s3_values;
  std::vector<double> y_values;

  TableS3(double d_min, double d_max) {
    s_min = 1. / d_max;
    s_max = 1. / d_min;
    const double smax3 = s_max*s_max*s_max, smin3 = s_min*s_min*s_min;
    delta_s3 = 0.0005;
    n_points = (int) ((smax3 - smin3) / delta_s3);
    n_points = std::max(20, std::min(2000, n_points));
    delta_s3 = (smax3 - smin3) / n_points;
    s3_values.reserve(n_points+1);
    y_values.reserve(n_points+1);
    for (int i = 0; i <= n_points; ++i)
      s3_values.push_back(smin3 + i * delta_s3);
  }

  // From Refmac SUBROUTINE D2DA_RADIAL_BIN in hkon_secder_tch.f
  void make_table(const std::vector<double> &svals, const std::vector<double> &yvals) {
    assert(svals.size() == yvals.size());

    // define bin
    const double smax_ml = *std::max_element(svals.begin(), svals.end()) * 1.0001;
    const double smin_ml = *std::min_element(svals.begin(), svals.end()) * 0.9999;
    const double smin_ml3 = smin_ml * smin_ml * smin_ml;
    const double smax_ml3 = smax_ml * smax_ml * smax_ml;

    // from Refmac SUBROUTINE DEFINE_BINS_FOR_ML in oppro_allocate.f
    const double binsize_ml = 0.0005;
    int nbin_ml =  std::max(1, std::min(500,
                                        (int)((smax_ml*smax_ml - smin_ml*smin_ml)/binsize_ml)+1
                                        ));
    int nbin_rad = nbin_ml;
    const double ds3 = (smax_ml3 - smin_ml3) / nbin_rad;

    std::vector<double> smeanb_rad(nbin_rad+1);
    for (int i = 0; i <= nbin_rad; ++i)
      smeanb_rad[i] = smin_ml3 + i * ds3;

    std::vector<double> sec_der_bin(nbin_rad+1);
    std::vector<int> nref_sec_bin(nbin_rad+1);
    for (size_t i = 0; i < svals.size(); ++i) {
      const double s = svals[i];
      const int ibin = std::upper_bound(smeanb_rad.begin(), smeanb_rad.end(), s*s*s) - smeanb_rad.begin() - 1;
      // int ibin;
      // for (int j = 0; j < nbin_rad; ++j)
      //   if (s3 > smeanb_rad[j] && s3 <= smeanb_rad[j+1]) {
      //     ibin = j;
      //     int ibin2 = std::upper_bound(smeanb_rad.begin(), smeanb_rad.end(), s*s*s) - smeanb_rad.begin();
      //     std::cout << "ibin= " << j << " diff= " << ibin2-j << "\n";
      //     break;
      //   }

      sec_der_bin[ibin] += yvals[i];
      nref_sec_bin[ibin] += 1;
    }

    for (int i = 0; i < nbin_rad; ++i)
      if (sec_der_bin[i] > 0 && nref_sec_bin[i] > 0)
        sec_der_bin[i] = std::log(sec_der_bin[i] / nref_sec_bin[i]);

    // sort out bins with no (usable) reflections
    for (int i = 1; i < nbin_rad; ++i)
      if (nref_sec_bin[i] <= 0 && nref_sec_bin[i-1] > 0)
        sec_der_bin[i] = sec_der_bin[i-1];
    for (int i = nbin_rad-2; i >= 0; --i)
      if (nref_sec_bin[i] <= 0 && nref_sec_bin[i+1] > 0)
        sec_der_bin[i] = sec_der_bin[i+1];

    // Start refining parameters of smoothining function
    double tmp1 = sec_der_bin[0];
    sec_der_bin[nbin_rad] = sec_der_bin[nbin_rad-1];
    for (int i = 1; i < nbin_rad; ++i) {
      double tmp2 = sec_der_bin[i];
      sec_der_bin[i] = (tmp1 + tmp2)/2.0;
      tmp1 = tmp2;
    }

    // TODO smooth curve here?

    const double kernel_g_rad = (smeanb_rad[1] - smeanb_rad[0]) / 2.0;
    for (int i = 0; i <= n_points; ++i) {
      const double yval = smooth_gauss_d(kernel_g_rad, smeanb_rad, sec_der_bin, s3_values[i]).first;
      y_values.push_back(std::exp(yval));
    }
  }

  double get_value(double s) const {
    const double s3 = s*s*s;
    const int i =  std::max(0, std::min(n_points,
                                        (int)std::round((s3 - s3_values.front()) / delta_s3)));
    return y_values[i];
  }
};

template <typename Table>
struct LL{
  std::vector<Atom*> atoms;
  UnitCell cell;
  SpaceGroup *sg;
  std::vector<Transform> ncs;
  bool mott_bethe;
  bool refine_xyz;
  int adp_mode;
  bool refine_h;
  // table (distances x b values)
  std::vector<double> table_bs;
  std::vector<std::vector<double>> pp1; // for x-x diagonal
  std::vector<std::vector<double>> bb;  // for B-B diagonal
  std::vector<std::vector<double>> aa; // for B-B diagonal, aniso

  LL(UnitCell cell, SpaceGroup *sg, const std::vector<Atom*> &atoms, bool mott_bethe,
     bool refine_xyz, int adp_mode, bool refine_h)
    : atoms(atoms), cell(cell), sg(sg), mott_bethe(mott_bethe), refine_xyz(refine_xyz),
      adp_mode(adp_mode), refine_h(refine_h) {
    if (adp_mode < 0 || adp_mode > 2) fail("bad adp_mode");
    set_ncs({});
  }
  void set_ncs(const std::vector<Transform> &trs) {
    ncs.clear();
    ncs.push_back(Transform()); // make sure first is the identity op
    for (const auto &tr : trs)
      if (!tr.is_identity())
        ncs.push_back(tr);
  }

  // FFT-based gradient calculation: Murshudov et al. (1997) 10.1107/S0907444996012255
  // if cryo-EM SPA, den is the Fourier transform of (dLL/dAc-i dLL/dBc)*mott_bethe_factor/s^2
  // When b_add is given, den must have been sharpened
  std::vector<double> calc_grad(Grid<float> &den, double b_add) { // needs <double>?
    const size_t n_atoms = atoms.size();
    const size_t n_v = n_atoms * ((refine_xyz ? 3 : 0) + (adp_mode == 0 ? 0 : adp_mode == 1 ? 1 : 6));
    std::vector<double> vn(n_v, 0.);
    for (size_t i = 0; i < n_atoms; ++i) {
      const Atom &atom = *atoms[i];
      if (!refine_h && atom.is_hydrogen()) continue;
      const Element &el = atom.element;
      const auto coef = Table::get(el);
      using precal_aniso_t = decltype(coef.precalculate_density_aniso_b(SMat33<double>()));
      const bool has_aniso = atom.aniso.nonzero();
      if (adp_mode == 1 && has_aniso) fail("bad adp_mode");
      for (const Transform &tr : ncs) { //TODO to use cell images?
        const Fractional fpos = cell.fractionalize(Position(tr.apply(atom.pos)));
        const SMat33<double> b_aniso = atom.aniso.scaled(u_to_b()).added_kI(b_add).transformed_by(tr.mat);
        double b_max = atom.b_iso + b_add;
        if (has_aniso) {
          const auto eig = b_aniso.calculate_eigenvalues();
          b_max = std::max(std::max(eig[0], eig[1]), eig[2]);
        }
        const auto precal = coef.precalculate_density_iso(b_max,
                                                          mott_bethe ? -el.atomic_number() : 0.);
        const precal_aniso_t precal_aniso = has_aniso ? coef.precalculate_density_aniso_b(b_aniso,
                                                                                          mott_bethe ? -el.atomic_number() : 0.)
          : precal_aniso_t();

        const double radius = determine_cutoff_radius(it92_radius_approx(b_max),
                                                      precal, 1e-7); // TODO cutoff?
        const int N = sizeof(precal.a) / sizeof(precal.a[0]);
        const int du = (int) std::ceil(radius / den.spacing[0]);
        const int dv = (int) std::ceil(radius / den.spacing[1]);
        const int dw = (int) std::ceil(radius / den.spacing[2]);
        Position gx;
        double gb = 0.;
        double gb_aniso[6] = {0,0,0,0,0,0};
        den.template use_points_in_box<true>(fpos, du, dv, dw,
                                             [&](float& point, const Position& delta, int, int, int) {
                                               if (point == 0) return;
                                               const double r2 = delta.length_sq();
                                               if (r2 > radius * radius) return;
                                               if (!has_aniso) { // isotropic
                                                 double for_x = 0., for_b = 0.;
                                                 for (int j = 0; j < N; ++j) {
                                                   const double tmp = precal.a[j] * std::exp(precal.b[j] * r2) * precal.b[j];
                                                   for_x += tmp;
                                                   if (adp_mode == 1) for_b += tmp * (1.5 + r2 * precal.b[j]);
                                                 }
                                                 gx += for_x * 2 * delta * point;
                                                 if (adp_mode == 1) gb += for_b * point;
                                               } else { // anisotropic
                                                 for (int j = 0; j < N; ++j) {
                                                   const double tmp = precal_aniso.a[j] * std::exp(precal_aniso.b[j].r_u_r(delta));
                                                   const auto tmp2 = precal_aniso.b[j].multiply(delta); // -4pi^2 * (B+b)^-1 . delta
                                                   gx += 2 * tmp * Position(tmp2) * point;
                                                   if (adp_mode == 2) {
                                                     // d/dp |B| = |B| B^-T
                                                     const auto tmp3 = precal_aniso.b[j].scaled(0.5 * tmp * point).elements_pdb();
                                                     // d/dp r^T B^-1 r = ..
                                                     gb_aniso[0] += tmp3[0] + tmp2.x * tmp2.x * tmp * point;
                                                     gb_aniso[1] += tmp3[1] + tmp2.y * tmp2.y * tmp * point;
                                                     gb_aniso[2] += tmp3[2] + tmp2.z * tmp2.z * tmp * point;
                                                     gb_aniso[3] += 2 * tmp3[3] + 2 * tmp2.x * tmp2.y * tmp * point;
                                                     gb_aniso[4] += 2 * tmp3[4] + 2 * tmp2.x * tmp2.z * tmp * point;
                                                     gb_aniso[5] += 2 * tmp3[5] + 2 * tmp2.y * tmp2.z * tmp * point;
                                                   }
                                                 }
                                               }
                                             }, false /* fail_on_too_large_radius */);
        gx *= atom.occ;
        if (adp_mode == 1)
          gb *= atom.occ * 0.25 / sq(pi());
        else if (adp_mode == 2)
          for (int i = 0; i < 6; ++i)
            gb_aniso[i] *= atom.occ * 0.25 / sq(pi());

        if (refine_xyz) {
          const auto gx2 = tr.mat.transpose().multiply(gx);
          vn[3*i  ] += gx2.x;
          vn[3*i+1] += gx2.y;
          vn[3*i+2] += gx2.z;
        }
        const int offset = (refine_xyz ? n_atoms * 3 : 0);
        if (adp_mode == 1)
          vn[offset + i] += gb;
        else if (adp_mode == 2) { // added as B (not U)
          for (int j = 0; j < 6; ++j) {
            const auto m = SMat33<double>({double(j==0), double(j==1), double(j==2), double(j==3), double(j==4), double(j==5)}).transformed_by(tr.mat);
            vn[offset + 6*i+j] += (gb_aniso[0] * m.u11 + gb_aniso[1] * m.u22 + gb_aniso[2] * m.u33 +
                                   gb_aniso[3] * m.u12 + gb_aniso[4] * m.u13 + gb_aniso[5] * m.u23);
          }
        }
      }
    }
    for (auto &v : vn) // to match scale of hessian
      v *= (mott_bethe ? -1 : 1) / (double) ncs.size();
    return vn;
  }

  /*
  void add_fisher_diagonal_naive(const Vec3 &svec, double d2ll) {
    // TODO symmetry
    // TODO aniso
    const double s2 = svec.length_sq();
    for (size_t i = 0; i < atoms.size(); ++i) {
      const Atom &atom = *atoms[i];
      const auto coef = IT92<double>::get(atom.element); // as Table::?
      const double f = (atom.element.atomic_number() - coef.calculate_sf(s2/4))/s2; // only for mott_bethe
      const double w = atom.occ*atom.occ*f*f*std::exp(-atom.b_iso*s2/2)*d2ll * 4 * pi()*pi();
      const int ipos = i*6;
      for (int sign = 0; sign < 2; ++sign) { // Friedel pair
        const Vec3 s = sign ? -svec : svec;
        am[ipos]   += w * s.x * s.x;
        am[ipos+1] += w * s.y * s.y;
        am[ipos+2] += w * s.z * s.z;
        am[ipos+3] += w * s.y * s.x;
        am[ipos+4] += w * s.z * s.x;
        am[ipos+5] += w * s.z * s.y;
      }
    }
  }
  */

  // preparation for fisher_diag_from_table()
  // Steiner et al. (2003) doi: 10.1107/S0907444903018675
  void make_fisher_table_diag_fast(double b_min, double b_max,
                                   const TableS3 &d2dfw_table) {
    pp1.resize(1);
    bb.resize(1);
    aa.resize(1);
    const double b_step = 5;
    const double s_min = d2dfw_table.s_min, s_max = d2dfw_table.s_max;
    const int s_dim = 120; // actually +1 is allocated
    int b_dim = static_cast<int>((b_max - b_min) / b_step) + 2;
    if (b_dim % 2 == 0) ++b_dim; // TODO: need to set maximum b_dim?
    pp1[0].resize(b_dim);
    bb[0].resize(b_dim);
    aa[0].resize(b_dim);

    const double s_step = (s_max - s_min) / s_dim;

    table_bs.clear();
    table_bs.reserve(b_dim);

    // only for D = 0 (same atoms) for now
    for (int ib = 0; ib < b_dim; ++ib) {
      const double b = b_min + b_step * ib;
      table_bs.push_back(b);

      std::vector<double> tpp(s_dim+1), tbb(s_dim+1), taa(s_dim+1);
      for (int i = 0; i <= s_dim; ++i) {
        const double s = s_min + s_step * i;
        const double w_c = d2dfw_table.get_value(s); // average of weight
        const double w_c_ft_c = w_c * std::exp(-b*s*s/4.);
        tpp[i] = 16. * pi() * pi() * pi() * w_c_ft_c / 3.; // (2pi)^2 * 4pi/3
        tbb[i] = pi() / 4 * w_c_ft_c * s * s; // 1/16 * 4pi
        taa[i] = pi() / 20 * w_c_ft_c * s * s; // 1/16 * 4pi/5 (later *1, *1/3, *4/3)
        if (!mott_bethe) {
          tpp[i] *= s*s*s*s;
          tbb[i] *= s*s*s*s;
          taa[i] *= s*s*s*s;
        }
      }

      // Numerical integration by Simpson's rule
      double sum_tpp1 = 0, sum_tpp2 = 0, sum_tbb1 = 0, sum_tbb2 = 0, sum_taa1 = 0, sum_taa2 = 0;
      for (int i = 1; i < s_dim; i+=2) {
        sum_tpp1 += tpp[i];
        sum_tbb1 += tbb[i];
        sum_taa1 += taa[i];
      }
      for (int i = 2; i < s_dim; i+=2) {
        sum_tpp2 += tpp[i];
        sum_tbb2 += tbb[i];
        sum_taa2 += taa[i];
      }

      pp1[0][ib] = (tpp[0] + tpp.back() + 4 * sum_tpp1 + 2 * sum_tpp2) * s_step / 3.;
      bb[0][ib] = (tbb[0] + tbb.back() + 4 * sum_tbb1 + 2 * sum_tbb2) * s_step / 3.;
      aa[0][ib] = (taa[0] + taa.back() + 4 * sum_taa1 + 2 * sum_taa2) * s_step / 3.;
    }
  }

  // from Refmac SUBROUTINE LINTER_VALUE2
  // no need to be a member of this class
  double interp_1d(const std::vector<double> &x_points,
                   const std::vector<double> &y_points,
                   double x) const {
    assert(x_points.size() == y_points.size());
    assert(!x_points.empty());
    if (x < x_points.front() || x > x_points.back()) fail("bad x: " + std::to_string(x));

    if (x_points.size() == 1)
      return y_points.front();

    const int k1 = std::min((size_t) (std::lower_bound(x_points.begin(), x_points.end(), x) - x_points.begin()),
                            x_points.size()-2);

    // calculate value of function at the given point
    double b = y_points[k1];
    double a = (y_points[k1+1] - y_points[k1]) / (x_points[k1+1] - x_points[k1]);
    double dx = x - x_points[k1];
    double y = a * dx + b;
    if (x < x_points.front())
      return std::max(0.1 * y_points.front(), std::min(10.0 * y_points.front(), y));
    else if (x > x_points.back())
      return std::max(0.1 * y_points.back(), std::min(10.0 * y_points.back(), y));
    else
      return y;
  }

  std::vector<double> fisher_diag_from_table() {
    const size_t n_atoms = atoms.size();
    const size_t n_a = n_atoms * ((refine_xyz ? 3 : 0) + (adp_mode == 0 ? 0 : adp_mode == 1 ? 1 : 9));
    const int N = Table::Coef::ncoeffs;
    std::vector<double> am(n_a, 0.);
    for (size_t i = 0; i < n_atoms; ++i) {
      const Atom &atom = *atoms[i];
      if (!refine_h && atom.is_hydrogen()) continue;
      const auto coef = Table::get(atom.element);
      const double w = atom.occ * atom.occ;
      const double c = mott_bethe ? coef.c() - atom.element.atomic_number(): coef.c();
      const double b_iso = atom.aniso.nonzero() ? u_to_b() * atom.aniso.trace() / 3 : atom.b_iso;
      double fac_x = 0., fac_b = 0., fac_a = 0.;

      // TODO can be reduced for the same elements
      for (int j = 0; j < N + 1; ++j)
        for (int k = 0; k < N + 1; ++k) {
          // * -1 is needed for mott_bethe case, but we only need aj * ak so they cancel.
          const double aj = j < N ? coef.a(j) : c;
          const double ak = k < N ? coef.a(k) : c;
          const double b = 2 * b_iso + (j < N ? coef.b(j) : 0) + (k < N ? coef.b(k) : 0);
          fac_x += aj * ak * interp_1d(table_bs, pp1[0], b);
          fac_b += aj * ak * interp_1d(table_bs, bb[0], b);
          fac_a += aj * ak * interp_1d(table_bs, aa[0], b);
        }

      const int ipos = i*3;
      if (refine_xyz) am[ipos] = am[ipos+1] = am[ipos+2] = w * fac_x;
      const int offset = refine_xyz ? n_atoms * 3 : 0;
      if (adp_mode == 1)
        am[offset + i] = w * fac_b;
      else if (adp_mode == 2) {
        for (int j = 0; j < 3; ++j) am[offset + 9*i + j] = w * fac_a;     // 11-11, 22-22, 33-33
        for (int j = 3; j < 6; ++j) am[offset + 9*i + j] = w * fac_a * 4; // 12-12, 13-13, 23-23
        for (int j = 6; j < 9; ++j) am[offset + 9*i + j] = w * fac_a / 3; // 11-22, 11-33, 22-33
      }
    }
    return am;
  }

  void get_am_col_row(int *row, int *col) const {
    const size_t n_atoms = atoms.size();
    const size_t n_a = n_atoms * ((refine_xyz ? 3 : 0) + (adp_mode == 0 ? 0 : adp_mode == 1 ? 1 : 9));
    size_t i = 0, offset = 0;
    if (refine_xyz) {
      for (size_t j = 0; j < n_atoms; ++j)
        for (size_t k = 0; k < 3; ++k, ++i)
          row[i] = col[i] = 3*j + k;
      offset = 3 * n_atoms;
    }
    if (adp_mode == 1) {
      for (size_t j = 0; j < n_atoms; ++j, ++i)
        row[i] = col[i] = offset + j;
    } else if (adp_mode == 2) {
      for (size_t j = 0; j < n_atoms; ++j, i+=9) {
        for (size_t k = 0; k < 6; ++k)
          row[i+k] = col[i+k] = offset + 6 * j + k;
        // 11-22
        row[i+6] = offset + 6 * j;
        col[i+6] = offset + 6 * j + 1;
        // 11-33
        row[i+7] = offset + 6 * j;
        col[i+7] = offset + 6 * j + 2;
        // 22-33
        row[i+8] = offset + 6 * j + 1;
        col[i+8] = offset + 6 * j + 2;
      }
    }
    if(i != n_a) fail("wrong matrix size");
  }
};



} // namespace gemmi
#endif
