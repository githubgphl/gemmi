// Copyright 2018 Global Phasing Ltd.

#include "placeh.h"
#include <cmath> // for sqrt, sin, cos
#include <gemmi/chemcomp.hpp> // for ChemComp
#include <gemmi/calculate.hpp> // for calculate_angle

using gemmi::Topo;
using gemmi::Restraints;
using gemmi::Position;

struct BondedAtom {
  gemmi::Atom* ptr;
  gemmi::Position& pos; // == ptr->pos;
  double dist;
};

// Calculate position using one angle (theta) and one dihedral angle (tau).
// Returns position of x4 in x1-x2-x3-x4, where dist=|x3-x4| and
// theta is angle(x2, x3, x4).
// Based on section 3.3 of Paciorek et al, Acta Cryst. A52, 349 (1996).
static
Position position_from_angle_and_torsion(const Position& x1,
                                         const Position& x2,
                                         const Position& x3,
                                         double dist,  // |x3-x4|
                                         double theta, // angle x2-x3-x4
                                         double tau) { // dihedral angle
  using std::sin;
  using std::cos;
  gemmi::Vec3 u = x2 - x1;
  gemmi::Vec3 v = x3 - x2;
  gemmi::Vec3 e1 = v.normalized();
  double delta = u.dot(e1);
  gemmi::Vec3 e2 = -(u - delta * e1).normalized();
  gemmi::Vec3 e3 = e1.cross(e2);
  return x3 + Position(dist * (-cos(theta) * e1 +
                               sin(theta) * (cos(tau) * e2 + sin(tau) * e3)));
}

// Rodrigues' rotation formula, rotate vector v given axis of rotation
// (which must be a unit vector) and angle (in radians).
gemmi::Vec3 rotate_by_axis(const gemmi::Vec3& v, const gemmi::Vec3& axis,
                           double theta) {
  double sin_theta = std::sin(theta);
  double cos_theta = std::cos(theta);
  return v * cos_theta + axis.cross(v) * sin_theta +
         axis * (axis.dot(v) * (1 - cos_theta));
}

// Based on https://en.wikipedia.org/wiki/Trilateration
// If no points satisfy the returns NaNs
static
std::pair<Position, Position> trilaterate(const Position& p1, double r1sq,
                                          const Position& p2, double r2sq,
                                          const Position& p3, double r3sq) {
  // variables have the same names as on the Wikipedia Trilateration page
  gemmi::Vec3 ex = (p2 - p1).normalized();
	double i = ex.dot(p3-p1);
  gemmi::Vec3 ey = (gemmi::Vec3(p3) - p1 - i*ex).normalized();
  gemmi::Vec3 ez = ex.cross(ey);
	double d = (p2-p1).length();
	double j = ey.dot(p3-p1);
	double x = (r1sq - r2sq + d*d) / (2*d);
	double y = (r1sq - r3sq + i*i + j*j) / (2*j) - x*i/j;
  double z2 = r1sq - x*x - y*y;
	double z = std::sqrt(z2);  // may result in NaN
	return std::make_pair(p1 + Position(x*ex + y*ey + z*ez),
	                      p1 + Position(x*ex + y*ey - z*ez));
}

// Calculate position using two angles.
// Returns p4. Topology: p1 is bonded to p2, p3 and p4.
static std::pair<Position, Position>
position_from_two_angles(const Position& p1,
                         const Position& p2,
                         const Position& p3,
                         double dist14,     // |p4-p1|
                         double theta214,   // angle p2-p1-p4
                         double theta314) { // angle p3-p1-p4
  double d12sq = p1.dist_sq(p2);
  double d13sq = p1.dist_sq(p3);
  double d14sq = dist14 * dist14;
  // the law of cosines
  double d24sq = d14sq + d12sq - 2 * std::sqrt(d14sq * d12sq) * cos(theta214);
  double d34sq = d14sq + d13sq - 2 * std::sqrt(d14sq * d13sq) * cos(theta314);
  auto t = trilaterate(p1, d14sq, p2, d24sq, p3, d34sq);
  return t;
}


void place_hydrogens(const gemmi::Atom& atom, Topo::ResInfo& ri,
                     const Topo& topo) {
  using Angle = Restraints::Angle;

  // put atoms bonded to atom into two lists
  std::vector<BondedAtom> known; // heavy atoms with known positions
  std::vector<BondedAtom> hs;    // H atoms (unknown)
  for (const Topo::Force& force : ri.forces)
    if (force.rkind == Topo::RKind::Bond) {
      const Topo::Bond& t = topo.bonds[force.index];
      int n = Topo::has_atom(&atom, t);
      if (n == 0 || n == 1) {
        gemmi::Atom* other = t.atoms[1-n];
        auto& atom_list = other->is_hydrogen() ? hs : known;
        atom_list.push_back({other, other->pos, t.restr->value});
      }
    }

  if (hs.size() == 0)
    return;

  auto giveup = [&](const std::string& message) {
    for (BondedAtom& bonded_h : hs)
      bonded_h.ptr->occ = 0;
    gemmi::fail(message);
  };

  // ==== only hydrogens ====
  if (known.size() == 0) {
    // we can only arbitrarily pick directions of atoms
    for (BondedAtom& bonded_h : hs)
      bonded_h.ptr->occ = 0;
    hs[0].pos = atom.pos + Position(hs[0].dist, 0, 0);
    if (hs.size() > 1) {
      double theta = M_PI;
      if (const Angle* ang = topo.take_angle(hs[1].ptr, &atom, hs[0].ptr))
        theta = ang->radians();
      hs[1].pos = atom.pos + Position(hs[1].dist * cos(theta),
                                      hs[1].dist * sin(theta), 0);
    }
    if (hs.size() > 2) {
      if (hs.size() == 3) {
        // for now only NH3 (NH2.cif and NH3.cif) has such configuration,
        // so we are cheating here a little.
        double y = 2 * atom.pos.y - hs[1].pos.y;
        hs[2].pos = Position(hs[1].pos.x, y, hs[1].pos.z);
      } else if (hs.size() == 4) {
        // similarly, only CH4 (CH2.cif) and and NH4 (NH4.cif) are handled here
        const Angle* ang1 = topo.take_angle(hs[2].ptr, &atom, hs[0].ptr);
        const Angle* ang2 = topo.take_angle(hs[2].ptr, &atom, hs[1].ptr);
        double theta1 = gemmi::rad(ang1 ? ang1->value : 109.47122);
        double theta2 = gemmi::rad(ang2 ? ang2->value : 109.47122);
        auto pos = position_from_two_angles(atom.pos, hs[0].pos, hs[1].pos,
                                            hs[2].dist, theta1, theta2);
        hs[2].pos = pos.first;
        hs[3].pos = pos.second;
      }
    }

  // ==== one heavy atom and hydrogens ====
  } else if (known.size() == 1) {
    const BondedAtom& h = hs[0];
    const BondedAtom& heavy = known[0];
    const Restraints::Angle* angle = topo.take_angle(h.ptr, &atom, heavy.ptr);
    if (!angle)
      giveup("No angle restraint for " + h.ptr->name + ", giving up.\n");
    if (std::abs(angle->value - 180.0) < 0.5) {
      gemmi::Vec3 u = atom.pos - h.pos;
      h.pos = atom.pos + Position(u * (h.dist / u.length()));
      return;
    }
    double theta = angle->radians();
    double tau = 0.0;
    const gemmi::Atom* tau_end = nullptr;
    for (const Topo::Plane& plane : topo.planes) {
      if (plane.atoms.size() > 3 &&
          plane.has(h.ptr) && plane.has(&atom) && plane.has(heavy.ptr)) {
        for (const gemmi::Atom* a : plane.atoms) {
          if (a != h.ptr && a != &atom && a != heavy.ptr) {
            tau_end = a;
            break;
          }
        }
        break;
      }
    }
    if (!tau_end) {
      // using one dihedral angle
      for (const Topo::Torsion& tor : topo.torsions) {
        if (tor.atoms[0] == h.ptr && !tor.atoms[3]->is_hydrogen() &&
            tor.atoms[1] == &atom && tor.atoms[2] == heavy.ptr) {
          tau = gemmi::rad(tor.restr->value);
          tau_end = tor.atoms[3];
          break;
        } else if (tor.atoms[3] == h.ptr && !tor.atoms[0]->is_hydrogen() &&
                   tor.atoms[1] == heavy.ptr && tor.atoms[2] == &atom) {
          tau = gemmi::rad(tor.restr->value);
          tau_end = tor.atoms[0];
          break;
        }
      }
    }
    h.pos = position_from_angle_and_torsion(
        tau_end ? tau_end->pos : Position(0, 0, 0),
        heavy.pos, atom.pos, h.dist, theta, tau);
    h.ptr->occ = 0; // the position is not unique
    if (hs.size() > 1) {
      const Angle* angle2 = topo.take_angle(hs[1].ptr, &atom, heavy.ptr);
      const Angle* angleh = topo.take_angle(hs[2].ptr, &atom, h.ptr);
      if (hs.size() == 2) {
        /* TODO: symmetric
        printf("cccccc  %g %g %g %s\n",
                                     angle ? angle->value : 0,
                                     angle2 ? angle2->value : 0,
                                     angleh ? angleh->value : 0,
                                     tau_end ? "tau" : "nop");
        */
      } else if (hs.size() == 2) {
        // TODO
      }
      giveup("not implemented yet");
    }

  // ==== two heavy atoms and hydrogens ====
  } else if (known.size() == 2) {
    if (hs.size() > 2)
      giveup("Unusual: atom bonded to two heavy atoms and 3+ hydrogens.");
    const Angle* ang1 = topo.take_angle(hs[0].ptr, &atom, known[0].ptr);
    const Angle* ang2 = topo.take_angle(hs[0].ptr, &atom, known[1].ptr);
    const Angle* ang3 = topo.take_angle(known[0].ptr, &atom, known[1].ptr);

    if (!ang1 || !ang2)
      giveup("Missing angle restraint, giving up.\n");
    double theta1 = ang1->radians();
    double theta2 = ang2->radians();
    if (ang3) {
      // If all atoms are in the same plane (sum of angles is 360 degree)
      // the calculations can be simplified.
      double theta3 = ang3->radians();
      gemmi::Vec3 v12 = (known[0].pos - atom.pos);
      gemmi::Vec3 v13 = (known[1].pos - atom.pos);
      // theta3 is the ideal restraint value, cur_theta3 is the current value
      double cur_theta3 = calculate_angle_v(v12, v13);
      constexpr double two_pi = 2 * gemmi::pi();
      if (theta1 + theta2 + std::max(theta3, cur_theta3) + 0.01 > two_pi) {
        double ratio = (two_pi - cur_theta3) / (theta1 + theta2);
        gemmi::Vec3 axis = v13.cross(v12).normalized();
        gemmi::Vec3 v14 = rotate_by_axis(v12, axis, theta1 * ratio);
        hs[0].pos = atom.pos + Position(hs[0].dist / v14.length() * v14);
        return;
      }
    }
    auto possible = position_from_two_angles(atom.pos,
                                             known[0].pos, known[1].pos,
                                             hs[0].dist, theta1, theta2);
    hs[0].pos = possible.first;
    if (hs.size() == 1) {
      const Topo::Chirality* chir = topo.get_chirality(&atom);
      if (chir && chir->restr->chir != gemmi::ChiralityType::Both) {
        if (!chir->check())
          hs[0].pos = possible.second;
      } else {
        hs[0].ptr->occ = 0;
      }
    } else {
      hs[1].pos = possible.second;
    }

  } else {
    printf("[not done] %5s: %zd h %zd known\n",
           atom.name.c_str(), hs.size(), known.size());
    giveup("not implemented yet");
  }
}

#ifdef PLACEH_MAIN

#include <stdio.h>
#include <gemmi/cif.hpp>   // for read_file
#include <gemmi/util.hpp>  // for fail

using namespace gemmi;


void print_restraint_summary(const std::string& id, const ChemComp& cc) {
  printf("%-5s %-5s ", cc.name.c_str(), id.c_str());
  fflush(stdout);
  std::string heavy_atom;

  for (const Restraints::Bond& bond : cc.rt.bonds) {
    const Restraints::AtomId* other_end = nullptr;
    if (bond.id1 == id)
      other_end = &bond.id2;
    else if (bond.id2 == id)
      other_end = &bond.id1;
    if (other_end) {
      if (!heavy_atom.empty())
        fail("H atom with 2+ bonds");
      heavy_atom = other_end->atom;
    }
  }
  if (heavy_atom.empty())
    fail("non-bonded H");
  printf("%-4s ", heavy_atom.c_str());

  int angle_count = 0;
  for (const Restraints::Angle& angle : cc.rt.angles) {
    const Restraints::AtomId* other_end = nullptr;
    if (angle.id1 == id)
      other_end = &angle.id3;
    else if (angle.id3 == id)
      other_end = &angle.id1;
    if (other_end) {
      if (angle.id2 != heavy_atom)
        fail("_chem_comp_angle.atom_id_2 is not H's heavy atom.");
      //printf("%.1f deg to %s, ", angle.value, other_end->atom.c_str());
      if (!cc.get_atom(other_end->atom).is_hydrogen()) {
        ++angle_count;
      }
    }
  }

  int tor_count = 0;
  for (const Restraints::Torsion& tor : cc.rt.torsions) {
    //if (tor.period > 1) continue;
    const Restraints::AtomId* other[3] = {nullptr, nullptr, nullptr};
    if (tor.id1 == id) {
      other[0] = &tor.id2;
      other[1] = &tor.id3;
      other[2] = &tor.id4;
    } else if (tor.id4 == id) {
      other[0] = &tor.id3;
      other[1] = &tor.id2;
      other[2] = &tor.id1;
    }
    if (other[0]) {
      if (other[0]->atom != heavy_atom)
        fail("_chem_comp_tor atom next to H is not H's heavy atom.");
      if (!cc.get_atom(other[2]->atom).is_hydrogen())
        ++tor_count;
    }
  }

  int chir_count = 0;
  for (const Restraints::Chirality& chir : cc.rt.chirs) {
    if (chir.id1 == id || chir.id2 == id || chir.id3 == id) {
      if (chir.id_ctr != heavy_atom)
        fail("_chem_comp_chir atom next to H is not H's heavy atom.");
      ++chir_count;
    }
  }

  int plane_count = 0;
  for (const Restraints::Plane& plane : cc.rt.planes) {
    const std::vector<Restraints::AtomId>& ids = plane.ids;
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
      if (std::find(ids.begin(), ids.end(), heavy_atom) == ids.end())
        fail("H in _chem_comp_plane without its heavy atom.");
      ++plane_count;
    }
  }

  printf("%d angles, %d torsions, %d chiralities, %d planes\n",
         angle_count, tor_count, chir_count, plane_count);
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    cif::Document doc = cif::read_file(argv[i]);
    for (const cif::Block& block : doc.blocks)
      if (block.name != "comp_list") {
        ChemComp cc = make_chemcomp_from_block(block);
        for (const ChemComp::Atom& atom : cc.atoms)
          if (atom.el == El::H)
            try {
              print_restraint_summary(atom.id, cc);
            } catch (std::runtime_error& e) {
              fprintf(stderr, "%s %s:%s\n",
                      block.name.c_str(), atom.id.c_str(), e.what());
            }
      }
  }
}
#endif

// vim:sw=2:ts=2:et
