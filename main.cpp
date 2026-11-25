// ============================================================================
//  ERL-HUB : Post-Earthquake Multi-Agency Emergency Relief Logistics Hub
//  Course  : Object-Oriented Programming (CIC-211)  |  Language: C++17
//
//  Unit I   : namespaces, function overloading, call/return by reference,
//             inline utilities, friend class (IncidentCommander)
//  Unit II  : classes/ADTs, static members, copy ctor, dynamic memory,
//             destructors, operator overloading (+= -= == << >>)
//  Unit III : inheritance, virtual base classes (diamond), composition,
//             runtime polymorphism, virtual destructors
//  Unit IV  : custom exceptions, binary file I/O with atomic updates,
//             class template + STL + functors + I/O manipulators
//
//  Build:  g++ -std=c++17 -Wall -Wextra -o erl_hub main.cpp
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
//  SHARED BASICS  (namespace ReliefOps)
// ============================================================================
namespace ReliefOps {

struct Point {
    double x{0}, y{0};
    // Unit I: inline utility method for rapid distance calculation
    inline double distanceTo(const Point& o) const { return std::hypot(x - o.x, y - o.y); }
};

// Unit I: inline utility functions
inline double payloadTonnes(double kg) { return kg / 1000.0; }
inline double travelHours(double km, double kph) { return kph > 0 ? km / kph : 0.0; }
inline std::string num(double v, int prec = 1) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

// ---------------------------------------------------------------------------
//  Unit IV: Exception framework (all derive from std::exception)
// ---------------------------------------------------------------------------
class ReliefException : public std::exception {
    std::string msg_;
public:
    explicit ReliefException(std::string m) : msg_(std::move(m)) {}
    const char* what() const noexcept override { return msg_.c_str(); }
};

class CargoOverweightException : public ReliefException {
    double load_, cap_;
public:
    CargoOverweightException(const std::string& vehicle, double load, double cap)
        : ReliefException("CargoOverweight [" + vehicle + "]: " + num(load) + " kg exceeds " + num(cap) + " kg"),
          load_(load), cap_(cap) {}
    double excessKg() const { return load_ - cap_; }
};

class FuelDepletionException : public ReliefException {
    double needed_, avail_;
public:
    FuelDepletionException(const std::string& vehicle, double needed, double avail)
        : ReliefException("FuelDepletion [" + vehicle + "]: needs " + num(needed) + " L, has " + num(avail) + " L"),
          needed_(needed), avail_(avail) {}
    double shortfallLitres() const { return needed_ - avail_; }
};

class DamagedCorridorException : public ReliefException {
public:
    explicit DamagedCorridorException(const std::string& corridor)
        : ReliefException("DamagedCorridor: route '" + corridor + "' is impassable for this vehicle") {}
};

class AccessBarrierException : public ReliefException {
public:
    explicit AccessBarrierException(const std::string& depot)
        : ReliefException("AccessBarrier: depot '" + depot + "' is locked - IncidentCommander override required") {}
};

namespace Logistics { class IncidentCommander; }   // forward declaration (friend)

// ============================================================================
//  NAMESPACE  ReliefOps::Inventory
// ============================================================================
namespace Inventory {

// ---- binary helpers --------------------------------------------------------
inline void writeStr(std::ostream& os, const std::string& s) {
    std::uint32_t n = static_cast<std::uint32_t>(s.size());
    os.write(reinterpret_cast<const char*>(&n), sizeof n);
    os.write(s.data(), n);
}
inline std::string readStr(std::istream& is) {
    std::uint32_t n = 0;
    is.read(reinterpret_cast<char*>(&n), sizeof n);
    if (!is || n > 1000000) throw std::runtime_error("corrupt binary string");
    std::string s(n, '\0');
    if (n) is.read(&s[0], n);
    if (!is) throw std::runtime_error("truncated binary string");
    return s;
}
template <typename T> void writePod(std::ostream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof v);
}
template <typename T> T readPod(std::istream& is) {
    T v{};
    is.read(reinterpret_cast<char*>(&v), sizeof v);
    if (!is) throw std::runtime_error("unexpected end of binary stream");
    return v;
}

// ---- I/O manipulators (Unit IV) -------------------------------------------
inline int fmtIndex() { static int i = std::ios_base::xalloc(); return i; }
inline std::ostream& tabular(std::ostream& os)   { os.iword(fmtIndex()) = 1; return os; }
inline std::ostream& serialized(std::ostream& os){ os.iword(fmtIndex()) = 0; return os; }
struct colw { int w; };                                      // parameterised manipulator
inline std::ostream& operator<<(std::ostream& os, colw c) { return os << std::left << std::setw(c.w); }
inline std::ostream& rule(std::ostream& os) { return os << std::string(78, '-') << '\n'; }
inline std::ostream& cargoHeader(std::ostream& os) {
    os << colw{10} << "LOT" << colw{18} << "ITEM" << colw{16} << "TYPE" << colw{7} << "QTY"
       << colw{10} << "VOL(m3)" << colw{10} << "WT(kg)" << "PRI\n";
    return os << rule;
}

// ---- supplies -------------------------------------------------------------
enum class SupplyType { Rations = 0, TraumaMedicine = 1, CleanWaterTank = 2, ThermalBlanket = 3 };
inline const char* toString(SupplyType t) {
    switch (t) {
        case SupplyType::Rations:        return "Rations";
        case SupplyType::TraumaMedicine: return "TraumaMedicine";
        case SupplyType::CleanWaterTank: return "CleanWaterTank";
        case SupplyType::ThermalBlanket: return "ThermalBlanket";
    }
    return "Unknown";
}

// Abstract data type for one lot of relief cargo
class CargoItem {
    std::string lot_, name_;
    SupplyType type_;
    int qty_;
    double unitVol_, unitWt_;
    int priority_;                         // 1 (low) .. 5 (critical)
public:
    CargoItem() : type_(SupplyType::Rations), qty_(0), unitVol_(0), unitWt_(0), priority_(1) {}
    CargoItem(std::string lot, std::string name, SupplyType t, int qty, double uv, double uw, int pr)
        : lot_(std::move(lot)), name_(std::move(name)), type_(t), qty_(qty),
          unitVol_(uv), unitWt_(uw), priority_(pr) {
        if (qty < 0) throw std::invalid_argument("negative quantity");
    }
    const std::string& lot() const { return lot_; }
    const std::string& name() const { return name_; }
    SupplyType type() const { return type_; }
    int quantity() const { return qty_; }
    double unitVolume() const { return unitVol_; }
    double unitWeight() const { return unitWt_; }
    int priority() const { return priority_; }
    bool isMedical() const { return type_ == SupplyType::TraumaMedicine; }

    inline double totalVolume() const { return qty_ * unitVol_; }   // inline utilities
    inline double totalWeight() const { return qty_ * unitWt_; }

    void adjust(int delta) {
        if (qty_ + delta < 0) throw std::invalid_argument("insufficient quantity in lot " + lot_);
        qty_ += delta;
    }
    CargoItem withQuantity(int q) const { CargoItem c(*this); c.qty_ = q; return c; }

    void writeBinary(std::ostream& os) const {
        writeStr(os, lot_); writeStr(os, name_);
        writePod<std::int32_t>(os, static_cast<std::int32_t>(type_));
        writePod<std::int32_t>(os, qty_);
        writePod<double>(os, unitVol_); writePod<double>(os, unitWt_);
        writePod<std::int32_t>(os, priority_);
    }
    void readBinary(std::istream& is) {
        std::string l = readStr(is), n = readStr(is);
        auto t = readPod<std::int32_t>(is); auto q = readPod<std::int32_t>(is);
        auto v = readPod<double>(is);       auto w = readPod<double>(is);
        auto p = readPod<std::int32_t>(is);
        *this = CargoItem(l, n, static_cast<SupplyType>(t), q, v, w, p);
    }

    // Two entries are "the same cargo" if they carry the same lot number
    friend bool operator==(const CargoItem& a, const CargoItem& b) { return a.lot_ == b.lot_; }

    // text serialization: lot|name|type|qty|unitVol|unitWt|priority
    friend std::ostream& operator<<(std::ostream& os, const CargoItem& c) {
        return os << c.lot_ << '|' << c.name_ << '|' << static_cast<int>(c.type_) << '|' << c.qty_
                  << '|' << c.unitVol_ << '|' << c.unitWt_ << '|' << c.priority_;
    }
    friend std::istream& operator>>(std::istream& is, CargoItem& c) {
        std::string line;
        if (!std::getline(is, line) || line.empty()) { is.setstate(std::ios::failbit); return is; }
        std::stringstream ss(line);
        std::string f[7];
        for (auto& s : f)
            if (!std::getline(ss, s, '|')) { is.setstate(std::ios::failbit); return is; }
        try {
            c = CargoItem(f[0], f[1], static_cast<SupplyType>(std::stoi(f[2])), std::stoi(f[3]),
                          std::stod(f[4]), std::stod(f[5]), std::stoi(f[6]));
        } catch (...) { is.setstate(std::ios::failbit); }
        return is;
    }
};

inline void cargoRow(std::ostream& os, const CargoItem& c) {
    auto f = os.flags(); auto p = os.precision();
    os << colw{10} << c.lot() << colw{18} << c.name() << colw{16} << toString(c.type())
       << colw{7} << c.quantity() << std::fixed << std::setprecision(2)
       << colw{10} << c.totalVolume() << colw{10} << c.totalWeight() << c.priority() << '\n';
    os.flags(f); os.precision(p);
}
template <typename It> void printCargoTable(std::ostream& os, It b, It e) {
    os << cargoHeader;
    for (; b != e; ++b) cargoRow(os, *b);
}

// ---- transport manifest: DYNAMIC memory + rule of three --------------------
class TransportManifest {
    std::string id_;
    CargoItem* items_;
    std::size_t size_, cap_;

    void grow() {
        std::size_t nc = cap_ ? cap_ * 2 : 4;
        CargoItem* n = new CargoItem[nc];
        for (std::size_t i = 0; i < size_; ++i) n[i] = items_[i];
        delete[] items_;
        items_ = n; cap_ = nc;
    }
    int indexOf(const std::string& lot) const {
        for (std::size_t i = 0; i < size_; ++i) if (items_[i].lot() == lot) return static_cast<int>(i);
        return -1;
    }
public:
    explicit TransportManifest(std::string id = "UNASSIGNED")
        : id_(std::move(id)), items_(nullptr), size_(0), cap_(0) {}

    TransportManifest(const TransportManifest& o)                 // deep-copy constructor
        : id_(o.id_), items_(o.cap_ ? new CargoItem[o.cap_] : nullptr), size_(o.size_), cap_(o.cap_) {
        for (std::size_t i = 0; i < size_; ++i) items_[i] = o.items_[i];
    }
    TransportManifest& operator=(const TransportManifest& o) {
        if (this != &o) {
            CargoItem* n = o.cap_ ? new CargoItem[o.cap_] : nullptr;
            for (std::size_t i = 0; i < o.size_; ++i) n[i] = o.items_[i];
            delete[] items_;
            items_ = n; size_ = o.size_; cap_ = o.cap_; id_ = o.id_;
        }
        return *this;
    }
    ~TransportManifest() { delete[] items_; }

    const std::string& id() const { return id_; }
    std::size_t size() const { return size_; }
    const CargoItem* begin() const { return items_; }
    const CargoItem* end() const { return items_ + size_; }

    double totalWeight() const { double w = 0; for (auto& c : *this) w += c.totalWeight(); return w; }
    double totalVolume() const { double v = 0; for (auto& c : *this) v += c.totalVolume(); return v; }

    // += : add cargo (duplicate lot numbers are rejected)
    TransportManifest& operator+=(const CargoItem& c) {
        if (indexOf(c.lot()) != -1) throw std::invalid_argument("duplicate cargo lot " + c.lot());
        if (size_ == cap_) grow();
        items_[size_++] = c;
        return *this;
    }
    // -= : deduct cargo (e.g. on departure / delivery)
    TransportManifest& operator-=(const CargoItem& c) {
        int i = indexOf(c.lot());
        if (i < 0) throw std::out_of_range("lot " + c.lot() + " not on manifest " + id_);
        items_[i].adjust(-c.quantity());
        if (items_[i].quantity() == 0) {
            for (std::size_t k = static_cast<std::size_t>(i); k + 1 < size_; ++k) items_[k] = items_[k + 1];
            --size_;
        }
        return *this;
    }
    // == : same manifest identity and same cargo contents (order-insensitive)
    bool operator==(const TransportManifest& o) const {
        if (id_ != o.id_ || size_ != o.size_) return false;
        for (auto& c : *this) {
            int j = o.indexOf(c.lot());
            if (j < 0 || o.items_[j].quantity() != c.quantity()) return false;
        }
        return true;
    }
    bool operator!=(const TransportManifest& o) const { return !(*this == o); }
    // duplicate-cargo check between two manifests
    bool sharesCargoWith(const TransportManifest& o) const {
        for (auto& c : *this) if (o.indexOf(c.lot()) != -1) return true;
        return false;
    }

    // binary audit record
    void writeBinary(std::ostream& os) const {
        writeStr(os, id_);
        writePod<std::uint64_t>(os, size_);
        for (auto& c : *this) c.writeBinary(os);
    }
    void readBinary(std::istream& is) {
        std::string id = readStr(is);
        auto n = readPod<std::uint64_t>(is);
        if (n > 1000000) throw std::runtime_error("corrupt manifest size");
        TransportManifest tmp(id);
        for (std::uint64_t i = 0; i < n; ++i) { CargoItem c; c.readBinary(is); tmp += c; }
        *this = tmp;
    }

    // << : serialized text by default, table when the `tabular` manipulator is active
    friend std::ostream& operator<<(std::ostream& os, const TransportManifest& m) {
        if (os.iword(fmtIndex()) == 1) {
            os << "Manifest " << m.id_ << "  (" << m.size_ << " lots)\n";
            printCargoTable(os, m.begin(), m.end());
            os << rule << "Total weight: " << num(m.totalWeight()) << " kg | volume: "
               << num(m.totalVolume(), 2) << " m3\n";
        } else {
            os << "MANIFEST " << m.id_ << ' ' << m.size_ << '\n';
            for (auto& c : m) os << c << '\n';
        }
        return os;
    }
    friend std::istream& operator>>(std::istream& is, TransportManifest& m) {
        std::string tag, id; std::size_t n = 0;
        if (!(is >> tag >> id >> n) || tag != "MANIFEST") { is.setstate(std::ios::failbit); return is; }
        is.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        TransportManifest tmp(id);
        for (std::size_t i = 0; i < n; ++i) {
            CargoItem c;
            if (!(is >> c)) return is;
            tmp += c;
        }
        m = tmp;
        return is;
    }
};

// ---- distribution depot ----------------------------------------------------
class DistributionDepot {
    friend class ReliefOps::Logistics::IncidentCommander;   // can override access barrier
    std::string name_;
    Point loc_;
    std::vector<CargoItem> stock_;
    bool locked_;
public:
    DistributionDepot(std::string name, Point loc, bool locked = false)
        : name_(std::move(name)), loc_(loc), locked_(locked) {}

    const std::string& name() const { return name_; }
    const Point& location() const { return loc_; }
    bool isLocked() const { return locked_; }
    std::vector<CargoItem>& stock() { return stock_; }
    const std::vector<CargoItem>& stock() const { return stock_; }

    // Return by reference: caller can modify the depot's stock entry directly
    CargoItem& item(const std::string& lot) {
        for (auto& c : stock_) if (c.lot() == lot) return c;
        throw std::out_of_range("lot " + lot + " not stocked at " + name_);
    }
    const CargoItem& item(const std::string& lot) const {
        for (auto& c : stock_) if (c.lot() == lot) return c;
        throw std::out_of_range("lot " + lot + " not stocked at " + name_);
    }
    // += restock
    DistributionDepot& operator+=(const CargoItem& c) {
        for (auto& s : stock_) if (s == c) { s.adjust(c.quantity()); return *this; }
        stock_.push_back(c);
        return *this;
    }
    // -= deduct cargo for departing vehicle
    DistributionDepot& operator-=(const CargoItem& c) {
        if (locked_) throw AccessBarrierException(name_);
        item(c.lot()).adjust(-c.quantity());
        return *this;
    }
    friend std::ostream& operator<<(std::ostream& os, const DistributionDepot& d) {
        os << "Depot " << d.name_ << (d.locked_ ? " [LOCKED]" : "") << " @("
           << d.loc_.x << ',' << d.loc_.y << ")\n";
        printCargoTable(os, d.stock_.begin(), d.stock_.end());
        return os;
    }
};

}  // namespace Inventory

// ============================================================================
//  NAMESPACE  ReliefOps::Logistics
// ============================================================================
namespace Logistics {

using Inventory::CargoItem;
using Inventory::DistributionDepot;
using Inventory::TransportManifest;

struct Corridor {
    std::string name;
    Point from, to;
    bool roadDamaged;
    bool flooded;
    double km() const { return from.distanceTo(to); }
};

// ---- Unit I: overloaded allocation routines ---------------------------------
// (1) volume based: allocate as many units as fit in `volumeM3` (stock by reference)
inline int allocateSupply(CargoItem& stock, double volumeM3) {
    if (stock.unitVolume() <= 0) return 0;
    int units = std::min(stock.quantity(), static_cast<int>(std::floor(volumeM3 / stock.unitVolume())));
    stock.adjust(-units);
    return units;
}
// (2) volume AND weight limit
inline int allocateSupply(CargoItem& stock, double volumeM3, double weightLimitKg) {
    if (stock.unitVolume() <= 0 || stock.unitWeight() <= 0) return 0;
    int byVol = static_cast<int>(std::floor(volumeM3 / stock.unitVolume()));
    int byWt  = static_cast<int>(std::floor(weightLimitKg / stock.unitWeight()));
    int units = std::min({stock.quantity(), byVol, byWt});
    stock.adjust(-units);
    return units;
}
// (3) priority-based emergency medical triage: medical first, then highest priority
inline std::vector<CargoItem> allocateSupply(std::vector<CargoItem>& pool, int minPriority, int maxUnits) {
    std::vector<CargoItem*> cand;
    for (auto& c : pool) if (c.priority() >= minPriority && c.quantity() > 0) cand.push_back(&c);
    std::sort(cand.begin(), cand.end(), [](const CargoItem* a, const CargoItem* b) {
        if (a->isMedical() != b->isMedical()) return a->isMedical();
        return a->priority() > b->priority();
    });
    std::vector<CargoItem> out;
    for (auto* c : cand) {
        if (maxUnits <= 0) break;
        int take = std::min(c->quantity(), maxUnits);
        out.push_back(c->withQuantity(take));
        c->adjust(-take);
        maxUnits -= take;
    }
    return out;
}

// ---- components used by composition (Unit III) ------------------------------
class GPSBeacon {
    std::string id_; Point pos_; int pings_;
public:
    GPSBeacon(std::string id, Point p) : id_(std::move(id)), pos_(p), pings_(0) {}
    void ping(const Point& p) { pos_ = p; ++pings_; }
    const Point& position() const { return pos_; }
    friend std::ostream& operator<<(std::ostream& os, const GPSBeacon& g) {
        return os << g.id_ << " @(" << g.pos_.x << ',' << g.pos_.y << ") pings=" << g.pings_;
    }
};

class EmergencyLifeSupportUnit {
    double oxygenHours_; int medKits_;
public:
    EmergencyLifeSupportUnit(double oxygen = 48, int kits = 10) : oxygenHours_(oxygen), medKits_(kits) {}
    void consume(double hours) { oxygenHours_ = std::max(0.0, oxygenHours_ - hours); }
    double oxygenHours() const { return oxygenHours_; }
    int medKits() const { return medKits_; }
};

// ---- Unit II: TransportVehicle (ADT with static members) --------------------
class TransportVehicle {
    friend class IncidentCommander;
protected:
    std::string id_, name_;
    double capacityKg_, fuelL_;
    TransportManifest manifest_;

    static double totalDispatchedKg_;
    static int operationalVehicles_;

    void recordDispatch(double kg) { totalDispatchedKg_ += kg; }
public:
    TransportVehicle(std::string id, std::string name, double capKg, double fuelL)
        : id_(std::move(id)), name_(std::move(name)), capacityKg_(capKg), fuelL_(fuelL),
          manifest_(id_ + "-MF") { ++operationalVehicles_; }
    TransportVehicle(const TransportVehicle& o)
        : id_(o.id_ + "-copy"), name_(o.name_), capacityKg_(o.capacityKg_), fuelL_(o.fuelL_),
          manifest_(o.manifest_) { ++operationalVehicles_; }
    virtual ~TransportVehicle() { --operationalVehicles_; }

    const std::string& id() const { return id_; }
    double fuel() const { return fuelL_; }
    double capacityKg() const { return capacityKg_; }
    TransportManifest& manifest() { return manifest_; }
    const TransportManifest& manifest() const { return manifest_; }

    // Load cargo from a depot (checks capacity first so nothing is deducted on failure)
    void loadFrom(DistributionDepot& depot, const CargoItem& req) {
        double projected = manifest_.totalWeight() + req.totalWeight();
        if (projected > capacityKg_) throw CargoOverweightException(id_, projected, capacityKg_);
        depot -= req;
        manifest_ += req;
    }
    void unload(const CargoItem& delivered) { manifest_ -= delivered; }

    static double dispatchedTonnage() { return payloadTonnes(totalDispatchedKg_); }
    static int operationalCount() { return operationalVehicles_; }
};
double TransportVehicle::totalDispatchedKg_ = 0.0;
int TransportVehicle::operationalVehicles_ = 0;

// ---- Unit III: ReliefVehicle hierarchy -----------------------------------------
class ReliefVehicle : public TransportVehicle {
protected:
    GPSBeacon beacon_;                         // composition (no deep inheritance)
    EmergencyLifeSupportUnit lifeSupport_;
public:
    ReliefVehicle(std::string id, std::string name, double cap, double fuel, Point start)
        : TransportVehicle(id, std::move(name), cap, fuel), beacon_(id + "-GPS", start) {}
    ~ReliefVehicle() override { std::cout << "    [~ReliefVehicle] " << id_ << " released\n"; }

    virtual std::string kind() const = 0;
    virtual double fuelPerKm() const = 0;
    virtual double evaluateRoute(const Corridor& c) const = 0;   // hours; may throw

    // Template-method dispatch: validates load, route, fuel
    virtual double dispatch(const Corridor& c) {
        double w = manifest_.totalWeight();
        if (w > capacityKg_) throw CargoOverweightException(id_, w, capacityKg_);
        double hours = evaluateRoute(c);
        double need = fuelPerKm() * c.km();
        if (fuelL_ < need) throw FuelDepletionException(id_, need, fuelL_);
        fuelL_ -= need;
        beacon_.ping(c.to);
        lifeSupport_.consume(hours);
        recordDispatch(w);
        return hours;
    }
    virtual void describe(std::ostream& os) const {
        os << "  " << std::left << std::setw(26) << kind() << id_ << " | fuel " << num(fuelL_)
           << " L | cargo " << num(manifest_.totalWeight()) << "/" << num(capacityKg_) << " kg | "
           << beacon_ << '\n';
    }
};

class AerialTransport : public virtual ReliefVehicle {
    double cruiseKph_;
public:
    AerialTransport(std::string id, std::string name, double cap, double fuel, Point s, double kph = 240)
        : ReliefVehicle(std::move(id), std::move(name), cap, fuel, s), cruiseKph_(kph) {}
    ~AerialTransport() override { std::cout << "    [~AerialTransport]\n"; }
    std::string kind() const override { return "AerialTransport"; }
    double fuelPerKm() const override { return 4.5; }
    double evaluateRoute(const Corridor& c) const override { return travelHours(c.km(), cruiseKph_); }
};

class GroundConvoy : public virtual ReliefVehicle {
    int trucks_;
public:
    GroundConvoy(std::string id, std::string name, double cap, double fuel, Point s, int trucks = 4)
        : ReliefVehicle(std::move(id), std::move(name), cap, fuel, s), trucks_(trucks) {}
    ~GroundConvoy() override { std::cout << "    [~GroundConvoy]\n"; }
    std::string kind() const override { return "GroundConvoy"; }
    double fuelPerKm() const override { return 0.6; }
    double evaluateRoute(const Corridor& c) const override {
        if (c.roadDamaged) throw DamagedCorridorException(c.name);
        return travelHours(c.km(), 55);
    }
    int trucks() const { return trucks_; }
};

class AmphibiousCraft : public virtual ReliefVehicle {
public:
    AmphibiousCraft(std::string id, std::string name, double cap, double fuel, Point s)
        : ReliefVehicle(std::move(id), std::move(name), cap, fuel, s) {}
    ~AmphibiousCraft() override { std::cout << "    [~AmphibiousCraft]\n"; }
    std::string kind() const override { return "AmphibiousCraft"; }
    double fuelPerKm() const override { return 0.9; }
    double evaluateRoute(const Corridor& c) const override {
        if (c.roadDamaged && !c.flooded) throw DamagedCorridorException(c.name);
        return travelHours(c.km(), c.flooded ? 30 : 50);
    }
};

// Diamond: ReliefVehicle is a VIRTUAL base shared by both parents
class AmphibiousMedicalUnit : public AmphibiousCraft, public GroundConvoy {
    int beds_;
public:
    AmphibiousMedicalUnit(std::string id, std::string name, double cap, double fuel, Point s, int beds)
        : ReliefVehicle(id, name, cap, fuel, s),
          AmphibiousCraft(id, name, cap, fuel, s),
          GroundConvoy(id, name, cap, fuel, s, 2), beds_(beds) {}
    ~AmphibiousMedicalUnit() override { std::cout << "    [~AmphibiousMedicalUnit]\n"; }
    // final overriders required to resolve the diamond ambiguity
    std::string kind() const override { return "AmphibiousMedicalUnit"; }
    double fuelPerKm() const override { return 1.1; }
    double evaluateRoute(const Corridor& c) const override {
        return AmphibiousCraft::evaluateRoute(c) * 1.2;   // medical equipment slows travel
    }
    void describe(std::ostream& os) const override {
        ReliefVehicle::describe(os);
        os << "      -> " << beds_ << " field-hospital beds, O2 " << num(lifeSupport_.oxygenHours())
           << " h, medkits " << lifeSupport_.medKits() << '\n';
    }
};

// ---- Unit I: friend class that overrides access barriers --------------------
class IncidentCommander {
public:
    void unlockDepot(DistributionDepot& d) const { d.locked_ = false; }
    void emergencyRefuel(TransportVehicle& v, double litres) const { v.fuelL_ += litres; }
    void overrideCapacity(TransportVehicle& v, double kg) const { v.capacityKg_ = kg; }
};

// ============================================================================
//  Unit IV : persistence (binary streams, atomic file updates)
// ============================================================================
struct TransitLogRecord {
    char vehicleId[16];
    char event[48];
    double tonnage;
    double hours;
    static TransitLogRecord make(const std::string& v, const std::string& e, double t, double h) {
        TransitLogRecord r{};
        std::strncpy(r.vehicleId, v.c_str(), sizeof r.vehicleId - 1);
        std::strncpy(r.event, e.c_str(), sizeof r.event - 1);
        r.tonnage = t; r.hours = h;
        return r;
    }
};
static_assert(std::is_trivially_copyable<TransitLogRecord>::value, "binary-safe record required");

class PersistenceManager {
    static constexpr std::uint32_t kLogMagic = 0x4C4F4731, kManifestMagic = 0x4D414E31;

    // write to <path>.tmp, flush, then rename over the target => readers never see a half-written file
    static void atomicWrite(const std::string& path, const std::function<void(std::ostream&)>& writer) {
        const std::string tmp = path + ".tmp";
        std::error_code ec;
        try {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot open " + tmp);
            writer(out);
            out.flush();
            if (!out) throw std::runtime_error("write failed for " + tmp);
        } catch (...) { fs::remove(tmp, ec); throw; }
        fs::rename(tmp, path, ec);
        if (ec) { fs::remove(tmp, ec); throw std::runtime_error("atomic replace failed"); }
    }
public:
    static void saveLogs(const std::string& path, const std::vector<TransitLogRecord>& recs) {
        atomicWrite(path, [&](std::ostream& os) {
            Inventory::writePod<std::uint32_t>(os, kLogMagic);
            Inventory::writePod<std::uint64_t>(os, recs.size());
            for (auto& r : recs) os.write(reinterpret_cast<const char*>(&r), sizeof r);
        });
    }
    static std::vector<TransitLogRecord> loadLogs(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + path);
        if (Inventory::readPod<std::uint32_t>(in) != kLogMagic) throw std::runtime_error("bad log file");
        auto n = Inventory::readPod<std::uint64_t>(in);
        if (n > 10000000) throw std::runtime_error("corrupt log count");
        std::vector<TransitLogRecord> v(static_cast<std::size_t>(n));
        for (auto& r : v) {
            in.read(reinterpret_cast<char*>(&r), sizeof r);
            if (!in) throw std::runtime_error("truncated log file");
        }
        return v;
    }
    static void saveManifests(const std::string& path, const std::vector<TransportManifest>& ms) {
        atomicWrite(path, [&](std::ostream& os) {
            Inventory::writePod<std::uint32_t>(os, kManifestMagic);
            Inventory::writePod<std::uint64_t>(os, ms.size());
            for (auto& m : ms) m.writeBinary(os);
        });
    }
    static std::vector<TransportManifest> loadManifests(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + path);
        if (Inventory::readPod<std::uint32_t>(in) != kManifestMagic) throw std::runtime_error("bad manifest file");
        auto n = Inventory::readPod<std::uint64_t>(in);
        if (n > 1000000) throw std::runtime_error("corrupt manifest count");
        std::vector<TransportManifest> out;
        for (std::uint64_t i = 0; i < n; ++i) { TransportManifest m; m.readBinary(in); out.push_back(m); }
        return out;
    }
};

// ============================================================================
//  Unit IV : generic PriorityDispatchQueue<T> + functors
// ============================================================================
struct DispatchOrder {
    std::string id, destination;
    int priority;     // 1..5
    bool medical;
    friend std::ostream& operator<<(std::ostream& os, const DispatchOrder& d) {
        return os << Inventory::colw{8} << d.id << Inventory::colw{26} << d.destination
                  << Inventory::colw{5} << d.priority << (d.medical ? "MEDICAL" : "general");
    }
};

// Functor objects: return true if `a` should be served BEFORE `b`
struct OrderUrgency {
    bool operator()(const DispatchOrder& a, const DispatchOrder& b) const {
        if (a.medical != b.medical) return a.medical;
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id < b.id;
    }
};
struct CargoUrgency {
    bool operator()(const CargoItem& a, const CargoItem& b) const {
        if (a.isMedical() != b.isMedical()) return a.isMedical();
        if (a.priority() != b.priority()) return a.priority() > b.priority();
        return a.lot() < b.lot();
    }
};

template <typename T, typename Compare = std::less<T>>
class PriorityDispatchQueue {
    std::vector<std::pair<std::string, T>> items_;          // STL vector holds the entries
    std::unordered_map<std::string, std::size_t> index_;    // STL unordered_map: key -> position
    Compare cmp_;
    void reindex() { index_.clear(); for (std::size_t i = 0; i < items_.size(); ++i) index_[items_[i].first] = i; }
public:
    explicit PriorityDispatchQueue(Compare c = Compare()) : cmp_(c) {}

    bool push(const std::string& key, const T& v) {
        if (index_.count(key)) return false;                // reject duplicate keys
        index_[key] = items_.size();
        items_.emplace_back(key, v);
        return true;
    }
    bool contains(const std::string& key) const { return index_.count(key) != 0; }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }

    T pop() {                                               // removes most urgent element
        if (items_.empty()) throw std::underflow_error("dispatch queue empty");
        auto it = std::min_element(items_.begin(), items_.end(),
            [this](const std::pair<std::string, T>& a, const std::pair<std::string, T>& b) {
                return cmp_(a.second, b.second); });
        T v = it->second;
        items_.erase(it);
        reindex();
        return v;
    }
    std::vector<T> sorted() const {                         // snapshot in urgency order
        std::vector<T> out;
        for (auto& p : items_) out.push_back(p.second);
        std::sort(out.begin(), out.end(), cmp_);
        return out;
    }
};

}  // namespace Logistics
}  // namespace ReliefOps

// ============================================================================
//  DEMONSTRATION DRIVER
// ============================================================================
namespace Inv = ReliefOps::Inventory;
namespace Lg  = ReliefOps::Logistics;
using ReliefOps::Point;

static void section(const std::string& t) { std::cout << "\n==== " << t << " ====\n"; }

int main() {
    std::vector<Lg::TransitLogRecord> logs;
    Lg::IncidentCommander commander;

    // ---------------- Unit I & II : depot, overloading, references ------------
    section("UNIT I/II - Depot, references, overloaded allocation");
    Inv::DistributionDepot alpha("Alpha Relief Depot", {0, 0});
    alpha += Inv::CargoItem("R-100", "Ration Packs",      Inv::SupplyType::Rations,        2000, 0.010, 1.2,    2);
    alpha += Inv::CargoItem("M-200", "Trauma Kits",       Inv::SupplyType::TraumaMedicine,  300, 0.020, 2.5,    5);
    alpha += Inv::CargoItem("W-300", "Water Tanks (1kL)", Inv::SupplyType::CleanWaterTank,   40, 1.000, 1000.0, 3);
    alpha += Inv::CargoItem("B-400", "Thermal Blankets",  Inv::SupplyType::ThermalBlanket, 1500, 0.005, 0.8,    1);
    alpha += Inv::CargoItem("R-100", "Ration Packs",      Inv::SupplyType::Rations,         500, 0.010, 1.2,    2);  // restock merges
    std::cout << alpha;

    alpha.item("B-400").adjust(-100);                            // return-by-reference: modify stock in place
    int byVol = Lg::allocateSupply(alpha.item("R-100"), 5.0);    // overload 1 (call by reference)
    int byBoth = Lg::allocateSupply(alpha.item("B-400"), 2.0, 100.0);  // overload 2
    auto triage = Lg::allocateSupply(alpha.stock(), 4, 150);     // overload 3 (priority triage)
    std::cout << "\nVolume allocation (5 m3 of rations): " << byVol << " packs\n"
              << "Volume+weight allocation (blankets): " << byBoth << " units\n"
              << "Triage allocation (priority>=4, 150 units):\n";
    Inv::printCargoTable(std::cout, triage.begin(), triage.end());
    std::cout << "Inline utils -> distance (0,0)-(30,40) = " << Point{0, 0}.distanceTo({30, 40})
              << " km, 2500 kg = " << ReliefOps::payloadTonnes(2500) << " t\n";

    // ---------------- Unit II : manifests & operators ---------------------------
    section("UNIT II - Manifest operators, copy constructor, serialization");
    Inv::TransportManifest mA("MF-A");
    mA += alpha.item("R-100").withQuantity(100);
    mA += alpha.item("M-200").withQuantity(40);
    Inv::TransportManifest mB = mA;                              // copy constructor (deep copy)
    mB += alpha.item("W-300").withQuantity(2);
    std::cout << "mA == mB ? " << std::boolalpha << (mA == mB) << "  | share cargo? " << mA.sharesCargoWith(mB) << '\n';
    try { mA += alpha.item("R-100").withQuantity(5); }
    catch (const std::exception& e) { std::cout << "Duplicate cargo rejected: " << e.what() << '\n'; }
    mB -= alpha.item("W-300").withQuantity(2);                   // deduct on departure
    std::cout << "After mB -= water: mA == mB ? " << (mA == mB) << "\n\n";
    std::cout << Inv::tabular << mA << Inv::serialized << "\nSerialized form:\n" << mA;
    std::stringstream ss; ss << mA;
    Inv::TransportManifest parsed; ss >> parsed;                 // operator>> round-trip
    std::cout << "Round-trip via << and >> equal? " << (parsed == mA) << '\n';

    // ---------------- Unit III : polymorphic fleet -------------------------------
    section("UNIT III - Polymorphic fleet (virtual base / composition)");
    std::cout << "Operational vehicles before: " << Lg::TransportVehicle::operationalCount() << '\n';
    std::vector<Lg::ReliefVehicle*> fleet{
        new Lg::AerialTransport("HELI-1", "Rescue Helicopter", 1200, 300, {0, 0}),
        new Lg::GroundConvoy("TRK-1", "All-terrain Convoy", 8000, 400, {0, 0}),
        new Lg::AmphibiousCraft("AMPH-1", "Flood Boat", 3000, 200, {0, 0}),
        new Lg::AmphibiousMedicalUnit("MED-1", "Mobile Hospital", 2500, 150, {0, 0}, 12)};
    std::cout << "Operational vehicles after : " << Lg::TransportVehicle::operationalCount() << "\n";

    const Lg::Corridor c1{"NDRF Base -> Sector Alpha", {0, 0}, {30, 40}, false, false};   // 50 km
    const Lg::Corridor c2{"Highway-9 (collapsed)",     {0, 0}, {120, 90}, true, false};   // 150 km
    const Lg::Corridor c3{"Riverbank Flood Route",     {0, 0}, {60, 80}, true, true};     // 100 km

    // ---------------- Unit IV : exceptions ---------------------------------------
    section("UNIT IV - Exception handling during dispatch");
    auto tryDispatch = [&](Lg::ReliefVehicle* v, const Lg::Corridor& c) {
        try {
            double h = v->dispatch(c);                           // virtual call
            std::cout << "OK    " << v->id() << " -> " << c.name << " in " << ReliefOps::num(h, 2) << " h\n";
            logs.push_back(Lg::TransitLogRecord::make(v->id(), "DISPATCH " + c.name,
                           ReliefOps::payloadTonnes(v->manifest().totalWeight()), h));
        } catch (const ReliefOps::FuelDepletionException& e) {
            std::cout << "FAIL  " << e.what() << " (short by " << ReliefOps::num(e.shortfallLitres()) << " L)\n";
        } catch (const ReliefOps::CargoOverweightException& e) {
            std::cout << "FAIL  " << e.what() << " (excess " << ReliefOps::num(e.excessKg()) << " kg)\n";
        } catch (const ReliefOps::ReliefException& e) {
            std::cout << "FAIL  " << e.what() << '\n';
        } catch (const std::exception& e) {
            std::cout << "ERROR " << e.what() << '\n';
        }
    };

    // 1) overweight: 10 water tanks = 10 000 kg on a 1 200 kg helicopter (depot stays untouched)
    try { fleet[0]->loadFrom(alpha, alpha.item("W-300").withQuantity(10)); }
    catch (const ReliefOps::CargoOverweightException& e) { std::cout << "FAIL  " << e.what() << '\n'; }
    std::cout << "Water tanks still in depot: " << alpha.item("W-300").quantity() << '\n';

    // 2) normal loading
    fleet[0]->loadFrom(alpha, alpha.item("M-200").withQuantity(60));
    fleet[0]->loadFrom(alpha, alpha.item("R-100").withQuantity(200));
    fleet[1]->loadFrom(alpha, alpha.item("W-300").withQuantity(6));
    fleet[2]->loadFrom(alpha, alpha.item("R-100").withQuantity(300));
    fleet[3]->loadFrom(alpha, alpha.item("M-200").withQuantity(40));

    // 3) locked depot -> AccessBarrierException -> friend IncidentCommander overrides it
    Inv::DistributionDepot bravo("Bravo Depot (rubble-blocked)", {50, 50}, true);
    bravo += Inv::CargoItem("B-900", "Thermal Blankets", Inv::SupplyType::ThermalBlanket, 800, 0.005, 0.8, 1);
    try { fleet[1]->loadFrom(bravo, bravo.item("B-900").withQuantity(100)); }
    catch (const ReliefOps::AccessBarrierException& e) { std::cout << "FAIL  " << e.what() << '\n'; }
    commander.unlockDepot(bravo);
    fleet[1]->loadFrom(bravo, bravo.item("B-900").withQuantity(100));
    std::cout << "Commander unlocked Bravo; blankets loaded onto TRK-1.\n\n";

    // 4) route evaluation through BASE POINTERS (runtime polymorphism)
    tryDispatch(fleet[1], c2);       // ground convoy on collapsed highway -> DamagedCorridor
    tryDispatch(fleet[1], c1);       // ok
    tryDispatch(fleet[2], c3);       // amphibious craft on flooded route -> ok
    tryDispatch(fleet[3], c3);       // medical unit
    tryDispatch(fleet[0], c2);       // helicopter: 150 km * 4.5 = 675 L > 300 L -> FuelDepletion
    commander.emergencyRefuel(*fleet[0], 500);
    std::cout << "Commander refuelled HELI-1 (+500 L)\n";
    tryDispatch(fleet[0], c2);       // now succeeds

    std::cout << "\nFleet status (virtual describe()):\n";
    for (const auto* v : fleet) v->describe(std::cout);
    std::cout << "\nStatic metrics -> total dispatched: " << ReliefOps::num(Lg::TransportVehicle::dispatchedTonnage(), 3)
              << " t | operational vehicles: " << Lg::TransportVehicle::operationalCount() << '\n';

    // ---------------- Unit IV : binary persistence with atomic updates -----------
    section("UNIT IV - Binary audit logs & atomic save/restore");
    std::vector<Inv::TransportManifest> audit;
    for (auto* v : fleet) audit.push_back(v->manifest());
    try {
        Lg::PersistenceManager::saveManifests("audit_manifests.bin", audit);
        Lg::PersistenceManager::saveLogs("transit_logs.bin", logs);
        std::cout << "Saved " << audit.size() << " manifests and " << logs.size() << " transit records.\n"
                  << "Temp file left behind? " << std::boolalpha
                  << (fs::exists("audit_manifests.bin.tmp") || fs::exists("transit_logs.bin.tmp")) << '\n';
        auto restoredM = Lg::PersistenceManager::loadManifests("audit_manifests.bin");
        auto restoredL = Lg::PersistenceManager::loadLogs("transit_logs.bin");
        bool same = restoredM.size() == audit.size();
        for (std::size_t i = 0; same && i < audit.size(); ++i) same = (restoredM[i] == audit[i]);
        std::cout << "Restored manifests identical to originals? " << same << '\n';
        for (auto& r : restoredL)
            std::cout << "  log: " << std::left << std::setw(8) << r.vehicleId << std::setw(40) << r.event
                      << ReliefOps::num(r.tonnage, 3) << " t, " << ReliefOps::num(r.hours, 2) << " h\n";
    } catch (const std::exception& e) { std::cout << "Persistence error: " << e.what() << '\n'; }

    // ---------------- Unit IV : template queue + functors + manipulators ---------
    section("UNIT IV - PriorityDispatchQueue<T>, functors, manipulators");
    Lg::PriorityDispatchQueue<Lg::DispatchOrder, Lg::OrderUrgency> orders;
    orders.push("O1", {"O1", "Sector Alpha shelter",  3, false});
    orders.push("O2", {"O2", "Field hospital (north)", 5, true});
    orders.push("O3", {"O3", "Riverside camp",        4, false});
    orders.push("O4", {"O4", "Dialysis clinic",       4, true});
    std::cout << "Duplicate key accepted? " << orders.push("O1", {"O1", "dup", 1, false}) << '\n';
    std::cout << Inv::colw{8} << "ORDER" << Inv::colw{26} << "DESTINATION" << Inv::colw{5} << "PRI" << "TYPE\n" << Inv::rule;
    while (!orders.empty()) std::cout << orders.pop() << '\n';

    Lg::PriorityDispatchQueue<Inv::CargoItem, Lg::CargoUrgency> cargoQ;
    for (auto& s : alpha.stock()) cargoQ.push(s.lot(), s);
    auto sortedCargo = cargoQ.sorted();
    std::cout << "\nDepot cargo by medical urgency:\n";
    Inv::printCargoTable(std::cout, sortedCargo.begin(), sortedCargo.end());

    // ---------------- fleet deallocation ------------------------------------------
    section("Fleet deallocation (virtual destructors via ReliefVehicle*)");
    for (auto* v : fleet) { std::cout << "  deleting " << v->id() << '\n'; delete v; }
    fleet.clear();
    std::cout << "Operational vehicles after deallocation: " << Lg::TransportVehicle::operationalCount() << '\n';
    return 0;
}
