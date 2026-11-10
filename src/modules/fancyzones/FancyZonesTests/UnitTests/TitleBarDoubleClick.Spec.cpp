#include "pch.h"

#include <FancyZonesLib/TitleBarDoubleClick.h>

#include <CppUnitTestLogger.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace FancyZonesUnitTests
{
    TEST_CLASS (DoubleClickDetectorUnitTests)
    {
        static constexpr unsigned long Delay = 500;
        static constexpr int Dx = 2;
        static constexpr int Dy = 2;
        const HWND window1 = reinterpret_cast<HWND>(0x1001);
        const HWND window2 = reinterpret_cast<HWND>(0x1002);

        TEST_METHOD (FirstClickIsNotDoubleClick)
        {
            DoubleClickDetector detector;
            Assert::IsFalse(detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy));
        }

        TEST_METHOD (SecondClickInTimeAndPlace)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsTrue(detector.OnButtonDown(window1, { 12, 8 }, 1000 + Delay, Delay, Dx, Dy));
        }

        TEST_METHOD (SecondClickTooLate)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsFalse(detector.OnButtonDown(window1, { 10, 10 }, 1000 + Delay + 1, Delay, Dx, Dy));
        }

        TEST_METHOD (SecondClickTooFar)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsFalse(detector.OnButtonDown(window1, { 13, 10 }, 1100, Delay, Dx, Dy));
            detector.Reset();
            detector.OnButtonDown(window1, { 10, 10 }, 2000, Delay, Dx, Dy);
            Assert::IsFalse(detector.OnButtonDown(window1, { 10, 13 }, 2100, Delay, Dx, Dy));
        }

        TEST_METHOD (SecondClickOnOtherWindow)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsFalse(detector.OnButtonDown(window2, { 10, 10 }, 1100, Delay, Dx, Dy));
        }

        TEST_METHOD (MissedSecondClickStartsNewPair)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsFalse(detector.OnButtonDown(window2, { 50, 50 }, 1100, Delay, Dx, Dy));
            Assert::IsTrue(detector.OnButtonDown(window2, { 50, 50 }, 1200, Delay, Dx, Dy));
        }

        TEST_METHOD (ThirdClickIsNotDoubleClick)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            Assert::IsTrue(detector.OnButtonDown(window1, { 10, 10 }, 1100, Delay, Dx, Dy));
            Assert::IsFalse(detector.OnButtonDown(window1, { 10, 10 }, 1200, Delay, Dx, Dy));
        }

        TEST_METHOD (ResetForgetsFirstClick)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 1000, Delay, Dx, Dy);
            detector.Reset();
            Assert::IsFalse(detector.OnButtonDown(window1, { 10, 10 }, 1100, Delay, Dx, Dy));
        }

        TEST_METHOD (TickCountWrap)
        {
            DoubleClickDetector detector;
            detector.OnButtonDown(window1, { 10, 10 }, 0xFFFFFF00ul, Delay, Dx, Dy);
            Assert::IsTrue(detector.OnButtonDown(window1, { 10, 10 }, 0x00000010ul, Delay, Dx, Dy));
        }
    };

    TEST_CLASS (ZoneWithLargestOverlapUnitTests)
    {
        static ZonesMap TwoColumns()
        {
            ZonesMap zones;
            zones.emplace(0, Zone(RECT{ 0, 0, 500, 1000 }, 0));
            zones.emplace(1, Zone(RECT{ 500, 0, 1000, 1000 }, 1));
            return zones;
        }

        TEST_METHOD (NoZones)
        {
            Assert::IsTrue(TitleBarDoubleClick::ZoneWithLargestOverlap({}, RECT{ 0, 0, 100, 100 }).empty());
        }

        TEST_METHOD (LargestOverlapWins)
        {
            const auto result = TitleBarDoubleClick::ZoneWithLargestOverlap(TwoColumns(), RECT{ 400, 100, 900, 500 });
            Assert::AreEqual(size_t{ 1 }, result.size());
            Assert::AreEqual(ZoneIndex{ 1 }, result[0]);
        }

        TEST_METHOD (RectInsideOneZone)
        {
            const auto result = TitleBarDoubleClick::ZoneWithLargestOverlap(TwoColumns(), RECT{ 100, 100, 200, 200 });
            Assert::AreEqual(size_t{ 1 }, result.size());
            Assert::AreEqual(ZoneIndex{ 0 }, result[0]);
        }

        TEST_METHOD (NoOverlap)
        {
            Assert::IsTrue(TitleBarDoubleClick::ZoneWithLargestOverlap(TwoColumns(), RECT{ 1100, 0, 1200, 100 }).empty());
        }

        TEST_METHOD (TouchingEdgeIsNoOverlap)
        {
            Assert::IsTrue(TitleBarDoubleClick::ZoneWithLargestOverlap(TwoColumns(), RECT{ 1000, 0, 1100, 100 }).empty());
        }

        TEST_METHOD (TieKeepsLowerIndex)
        {
            const auto result = TitleBarDoubleClick::ZoneWithLargestOverlap(TwoColumns(), RECT{ 400, 100, 600, 200 });
            Assert::AreEqual(size_t{ 1 }, result.size());
            Assert::AreEqual(ZoneIndex{ 0 }, result[0]);
        }
    };
    TEST_CLASS (SwallowedClickUnitTests)
    {
        TEST_METHOD (ReleaseOfNormalPressGoesThrough)
        {
            SwallowedClick click;
            click.OnButtonDown(false);
            Assert::IsFalse(click.OnButtonUp());
        }

        TEST_METHOD (ReleaseOfSwallowedPressIsSwallowed)
        {
            SwallowedClick click;
            click.OnButtonDown(true);
            Assert::IsTrue(click.OnButtonUp());
        }

        TEST_METHOD (OnlyOneReleaseIsSwallowed)
        {
            SwallowedClick click;
            click.OnButtonDown(true);
            click.OnButtonUp();
            Assert::IsFalse(click.OnButtonUp());
        }

        TEST_METHOD (NewPressCancelsPendingSwallow)
        {
            SwallowedClick click;
            click.OnButtonDown(true);
            click.OnButtonDown(false);
            Assert::IsFalse(click.OnButtonUp());
        }

        TEST_METHOD (ReleaseWithoutPressGoesThrough)
        {
            SwallowedClick click;
            Assert::IsFalse(click.OnButtonUp());
        }
    };

    TEST_CLASS (CaptionHitUnitTests)
    {
        TEST_METHOD (CaptionIsTitleBar)
        {
            Assert::IsTrue(TitleBarDoubleClick::IsCaptionHit(HTCAPTION));
        }

        TEST_METHOD (OtherAreasAreNotTitleBar)
        {
            for (LRESULT hit : { HTCLIENT, HTNOWHERE, HTTRANSPARENT, HTMAXBUTTON, HTMINBUTTON, HTCLOSE, HTSYSMENU, HTTOP, HTLEFT, HTERROR })
            {
                Assert::IsFalse(TitleBarDoubleClick::IsCaptionHit(hit));
            }
        }
    };
}
