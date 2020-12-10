// Copyright 2020 Global Phasing Ltd.

#include "common.h"
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include "gemmi/elem.hpp"
#include "gemmi/it92.hpp"
#include "gemmi/c4322.hpp"
#include "gemmi/sfcalc.hpp"   // for StructureFactorCalculator
#include "gemmi/dencalc.hpp"  // for DensityCalculator
#include "gemmi/fprime.hpp"   // for cromer_libermann_fprime_for_all_elements

namespace py = pybind11;
using gemmi::Element;

template<typename Table>
void add_sfcalc(py::module& m, const char* name) {
  using SFC = gemmi::StructureFactorCalculator<Table>;
  py::class_<SFC>(m, name)
    .def(py::init<const gemmi::UnitCell&>())
    .def_readwrite("addends", &SFC::addends)
    .def("calculate_sf_from_model", &SFC::calculate_sf_from_model)
    .def("mott_bethe_factor", &SFC::mott_bethe_factor)
    ;
}

template<typename Table>
void add_dencalc(py::module& m, const char* name) {
  using DenCalc = gemmi::DensityCalculator<Table, float>;
  py::class_<DenCalc>(m, name)
    .def(py::init<>())
    .def_readonly("grid", &DenCalc::grid)
    .def_readwrite("d_min", &DenCalc::d_min)
    .def_readwrite("rate", &DenCalc::rate)
    .def_readwrite("blur", &DenCalc::blur)
    .def_readwrite("r_cut", &DenCalc::r_cut)
    .def_readwrite("addends", &DenCalc::addends)
    .def("put_model_density_on_grid", &DenCalc::put_model_density_on_grid)
    .def("set_grid_cell_and_spacegroup", &DenCalc::set_grid_cell_and_spacegroup)
    .def("reciprocal_space_multiplier", &DenCalc::reciprocal_space_multiplier)
    .def("mott_bethe_factor", &DenCalc::mott_bethe_factor)
    ;
}

void add_sf(py::module& m) {
  using IT92 = gemmi::IT92<double>;

  py::class_<IT92::Coef>(m, "IT92Coef")
    .def_property_readonly("a", [](IT92::Coef& c) -> std::array<double,4> {
        return {{ c.a(0), c.a(1), c.a(2), c.a(3) }};
    })
    .def_property_readonly("b", [](IT92::Coef& c) -> std::array<double,4> {
        return {{ c.b(0), c.b(1), c.b(2), c.b(3) }};
    })
    .def_property_readonly("c", &IT92::Coef::c)
    .def("calculate_sf", &IT92::Coef::calculate_sf, py::arg("stol2"))
    .def("calculate_density_iso", &IT92::Coef::calculate_density_iso,
         py::arg("r2"), py::arg("B"))
    ;

  using C4322 = gemmi::C4322<double>;
  py::class_<C4322::Coef>(m, "C4322Coef")
    .def_property_readonly("a", [](C4322::Coef& c) -> std::array<double,5> {
        return {{ c.a(0), c.a(1), c.a(2), c.a(3), c.a(4) }};
    })
    .def_property_readonly("b", [](C4322::Coef& c) -> std::array<double,5> {
        return {{ c.b(0), c.b(1), c.b(2), c.b(3), c.b(4) }};
    })
    .def("calculate_sf", &C4322::Coef::calculate_sf, py::arg("stol2"))
    .def("calculate_density_iso", &C4322::Coef::calculate_density_iso,
         py::arg("r2"), py::arg("B"))
    ;

  py::class_<Element>(m, "Element")
    .def(py::init<const std::string &>())
    .def(py::init<int>())
    .def("__eq__",
         [](const Element &a, const Element &b) { return a.elem == b.elem; },
         py::is_operator())
#if PY_MAJOR_VERSION < 3  // in Py3 != is inferred from ==
    .def("__ne__",
         [](const Element &a, const Element &b) { return a.elem != b.elem; },
         py::is_operator())
#endif
    .def_property_readonly("name", &Element::name)
    .def_property_readonly("weight", &Element::weight)
    .def_property_readonly("covalent_r", &Element::covalent_r)
    .def_property_readonly("vdw_r", &Element::vdw_r)
    .def_property_readonly("atomic_number", &Element::atomic_number)
    .def_property_readonly("is_metal", &Element::is_metal)
    .def_property_readonly("it92", [](const Element& self) {
        return IT92::get_ptr(self.elem);
    }, py::return_value_policy::reference_internal)
    .def_property_readonly("c4322", [](const Element& self) {
        return C4322::get_ptr(self.elem);
    }, py::return_value_policy::reference_internal)
    .def("__repr__", [](const Element& self) {
        return "<gemmi.Element: " + std::string(self.name()) + ">";
    });

  py::class_<gemmi::Addends>(m, "Addends")
    .def("set", &gemmi::Addends::set)
    .def("get", &gemmi::Addends::get)
    .def("clear", &gemmi::Addends::clear)
    .def("add_cl_fprime", [](gemmi::Addends& self, double energy) {
        gemmi::add_cl_fprime_for_all_elements(&self.values[1], energy);
    }, py::arg("energy"))
    .def("subtract_z", &gemmi::Addends::subtract_z,
         py::arg("except_hydrogen")=false)
    ;

  add_sfcalc<IT92>(m, "StructureFactorCalculatorX");
  add_sfcalc<C4322>(m, "StructureFactorCalculatorE");
  add_dencalc<IT92>(m, "DensityCalculatorX");
  add_dencalc<C4322>(m, "DensityCalculatorE");
}