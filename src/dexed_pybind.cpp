#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/iostream.h>  // For redirecting std::cout
#include <cstring>
#include "dexed.h"

namespace py = pybind11;

class PyDexed {
public:
    PyDexed(unsigned char max_notes, unsigned short sample_rate)
        : synth(new Dexed(max_notes, sample_rate)) {}

    ~PyDexed() { delete synth; }

    void resetControllers() { synth->resetControllers(); }
    void setVelocityScale(unsigned char scale) { synth->setVelocityScale(scale); }
    void loadVoiceParameters(py::bytes sysex) {
        std::string s = sysex;
        if (!s.empty()) {
            std::vector<unsigned char> buf(s.begin(), s.end());
            synth->loadVoiceParameters(buf.data());
        }
    }
    void setGain(float gain) { synth->setGain(gain); }
    void keydown(unsigned char note, unsigned char velocity) { synth->keydown(note, velocity); }
    void keyup(unsigned char note) { synth->keyup(note); }
    py::array_t<short> getSamples(size_t frames) {
        std::vector<short> buffer(frames);

        // Redirect std::cout to Python's sys.stdout
        py::scoped_ostream_redirect stream(std::cout, py::module_::import("sys").attr("stdout"));

        synth->getSamples(buffer.data(), static_cast<unsigned short>(frames));
        return py::array_t<short>(frames, buffer.data());
    }

private:
    Dexed* synth;
};

PYBIND11_MODULE(dexed_py, m) {
    py::class_<PyDexed>(m, "Dexed")
        .def(py::init<unsigned char, unsigned short>())
        .def("resetControllers", &PyDexed::resetControllers)
        .def("setVelocityScale", &PyDexed::setVelocityScale)
        .def("loadVoiceParameters", &PyDexed::loadVoiceParameters)
        .def("setGain", &PyDexed::setGain)
        .def("keydown", &PyDexed::keydown)
        .def("keyup", &PyDexed::keyup)
        .def("getSamples", &PyDexed::getSamples);
}