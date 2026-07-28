// FiveBarLinkageRobot: kapselt Kinematik und Motoransteuerung fuer den
// Zweiarm-Gelenkroboter (5-Bar-Linkage) mit zwei Schrittmotoren.
//
// Koordinatensystem: Ursprung mittig zwischen den beiden Motorachsen,
// y-Achse zeigt von den Motoren weg in Richtung Zeichenflaeche.
//
// Verwendung:
//   MyStepperController controller;   // implementiert IStepperController
//   FiveBarLinkageRobot robot(controller, /*baseDistanceMm=*/47.31,
//                              /*upperArmLengthMm=*/45.0,
//                              /*forearmLengthMm=*/65.0);
//   robot.DrawLine(from, to);
//   while (robot.Tick()) {
//       // hier ggf. kurze Wartezeit bis zum naechsten Motorschritt
//   }

#include <algorithm>
#include <cmath>

struct Point {
    double x, y;
};

// Vom Aufrufer bereitgestellt: bewegt beide Motoren synchron und
// unabhaengig voneinander um je -1, 0 oder +1 Schritt.
class IStepperController {
public:
    virtual ~IStepperController() = default;
    virtual void Step(int deltaMotor1, int deltaMotor2) = 0;
};

class FiveBarLinkageRobot {
public:
    FiveBarLinkageRobot(IStepperController& controller,
                         double baseDistanceMm,
                         double upperArmLengthMm,
                         double forearmLengthMm,
                         double maxStepMm = 1.0,
                         long   stepsPerRev = 4096)
        : controller_(controller)
        , L1_(upperArmLengthMm)
        , L2_(forearmLengthMm)
        , maxStepMm_(maxStepMm)
        , stepsPerRev_(stepsPerRev)
    {
        B1_ = { -baseDistanceMm / 2.0, 0.0 };
        B2_ = {  baseDistanceMm / 2.0, 0.0 };
        thetaHome_ = M_PI / 2.0; // beide Oberarme parallel, senkrecht nach oben

        ForwardKinematics(thetaHome_, thetaHome_, home_);
    }

    // Stiftposition in der Home-Pose (beide Oberarme parallel).
    Point HomePosition() const { return home_; }

    // Startet eine neue Geradenbewegung. Muss danach per Tick()
    // abgearbeitet werden, bis diese false zurueckgibt.
    void DrawLine(const Point& from, const Point& to)
    {
        pathStart_ = from;
        pathEnd_   = to;

        double dx = pathEnd_.x - pathStart_.x;
        double dy = pathEnd_.y - pathStart_.y;
        double length = std::sqrt(dx * dx + dy * dy);
        n_ = std::max(1, static_cast<int>(std::ceil(length / maxStepMm_)));

        waypointIndex_ = 0;
        active_ = true;
        AdvanceToNextWaypoint();
    }

    // Fuehrt hoechstens einen Motorschritt pro Motor aus (ruft dafuer
    // ggf. IStepperController::Step auf).
    // Rueckgabe: true = es gibt noch etwas zu tun, weiter aufrufen.
    //            false = Bewegung abgeschlossen (oder Ziel unerreichbar).
    bool Tick()
    {
        if (!active_) return false;

        int d1 = ClampStep(targetSteps1_ - currentSteps1_);
        int d2 = ClampStep(targetSteps2_ - currentSteps2_);

        if (d1 != 0 || d2 != 0) {
            controller_.Step(d1, d2);
            currentSteps1_ += d1;
            currentSteps2_ += d2;
            return true;
        }

        // aktuelles Zwischenziel erreicht -> naechstes bestimmen
        if (waypointIndex_ >= n_) {
            active_ = false;
            return false;
        }
        AdvanceToNextWaypoint();
        return active_;
    }

private:
    struct JointAngles {
        double theta1, theta2;
        bool   reachable;
    };

    static int ClampStep(long diff)
    {
        if (diff > 0) return 1;
        if (diff < 0) return -1;
        return 0;
    }

    static bool CircleIntersection(const Point& c1, double r1,
                                    const Point& c2, double r2,
                                    Point& out1, Point& out2)
    {
        double dx = c2.x - c1.x;
        double dy = c2.y - c1.y;
        double d  = std::sqrt(dx * dx + dy * dy);

        if (d > r1 + r2 || d < std::fabs(r1 - r2) || d == 0.0) return false;

        double a  = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
        double h2 = r1 * r1 - a * a;
        if (h2 < 0.0) return false;
        double h = std::sqrt(h2);

        double xm = c1.x + a * dx / d;
        double ym = c1.y + a * dy / d;

        out1 = { xm + h * dy / d, ym - h * dx / d };
        out2 = { xm - h * dy / d, ym + h * dx / d };
        return true;
    }

    // Vorwaertskinematik: aus beiden Motorwinkeln die Stiftposition.
    // Waehlt bei zwei Loesungen die von der Basis weiter entfernte
    // (groesseres y = weiter Richtung Zeichenflaeche).
    bool ForwardKinematics(double theta1, double theta2, Point& P) const
    {
        Point E1{ B1_.x + L1_ * std::cos(theta1), B1_.y + L1_ * std::sin(theta1) };
        Point E2{ B2_.x + L1_ * std::cos(theta2), B2_.y + L1_ * std::sin(theta2) };

        Point s1, s2;
        if (!CircleIntersection(E1, L2_, E2, L2_, s1, s2)) return false;
        P = (s1.y > s2.y) ? s1 : s2;
        return true;
    }

    bool SolveArm(const Point& B, const Point& P, int elbowSign, double& thetaOut) const
    {
        double dx = P.x - B.x;
        double dy = P.y - B.y;
        double r  = std::sqrt(dx * dx + dy * dy);

        if (r > (L1_ + L2_) || r < std::fabs(L1_ - L2_)) return false;

        double phi  = std::atan2(dy, dx);
        double cosA = (L1_ * L1_ + r * r - L2_ * L2_) / (2.0 * L1_ * r);
        cosA = std::max(-1.0, std::min(1.0, cosA));
        double alpha = std::acos(cosA);

        thetaOut = phi + elbowSign * alpha;
        return true;
    }

    JointAngles InverseKinematics(const Point& P) const
    {
        JointAngles result{ 0.0, 0.0, false };
        double t1, t2;
        bool ok1 = SolveArm(B1_, P, +1, t1);
        bool ok2 = SolveArm(B2_, P, -1, t2);
        result.theta1    = t1;
        result.theta2    = t2;
        result.reachable = ok1 && ok2;
        return result;
    }

    long AngleToSteps(double angleRad, double homeAngleRad) const
    {
        return static_cast<long>(
            std::round((angleRad - homeAngleRad) / (2.0 * M_PI) * stepsPerRev_));
    }

    void AdvanceToNextWaypoint()
    {
        ++waypointIndex_;
        double t = static_cast<double>(waypointIndex_) / n_;
        Point P{ pathStart_.x + t * (pathEnd_.x - pathStart_.x),
                 pathStart_.y + t * (pathEnd_.y - pathStart_.y) };

        JointAngles a = InverseKinematics(P);
        if (!a.reachable) {
            active_ = false; // Ziel ausserhalb Arbeitsraum -> Abbruch
            return;
        }
        targetSteps1_ = AngleToSteps(a.theta1, thetaHome_);
        targetSteps2_ = AngleToSteps(a.theta2, thetaHome_);
    }

    IStepperController& controller_;
    Point  B1_, B2_;
    double L1_, L2_;
    double maxStepMm_;
    long   stepsPerRev_;
    double thetaHome_;
    Point  home_;

    Point pathStart_{}, pathEnd_{};
    int   n_ = 0;
    int   waypointIndex_ = 0;
    long  currentSteps1_ = 0, currentSteps2_ = 0;
    long  targetSteps1_  = 0, targetSteps2_  = 0;
    bool  active_ = false;
};

// -------------------- Beispiel --------------------

#include <iostream>

class ConsoleStepperController : public IStepperController {
public:
    void Step(int deltaMotor1, int deltaMotor2) override
    {
        std::cout << "Motor1: " << deltaMotor1
                   << "  Motor2: " << deltaMotor2 << "\n";
    }
};

int main()
{
    ConsoleStepperController controller;
    FiveBarLinkageRobot robot(controller,
                               /*baseDistanceMm=*/47.31,
                               /*upperArmLengthMm=*/45.0,
                               /*forearmLengthMm=*/65.0);

    Point home = robot.HomePosition();
    std::cout << "Home-Position: (" << home.x << ", " << home.y << ")\n";

    Point target{ 20.0, 90.0 };
    robot.DrawLine(home, target);

    while (robot.Tick()) {
        // In echt: hier kurze Wartezeit / naechster Timer-Tick
    }

    return 0;
}