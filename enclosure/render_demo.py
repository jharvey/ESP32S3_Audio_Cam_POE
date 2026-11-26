# Straight-on pictures of the case for the office demo page: one with the use card's front in the holder, one with its
# back, button caps in dark grey. Reads the parts case_clam.py wrote to out_clam. Writes demo_front.png, demo_back.png
# and demo_view.json (which part of the model, in mm, the pictures cover).
import json, os
import cadquery
import vtk
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.join(here, "out_clam")
beige, grey, dark, black, board = (0.87, 0.80, 0.66), (0.82, 0.82, 0.80), (0.30, 0.31, 0.34), (0.05, 0.05, 0.05), (0.08, 0.08, 0.09)
KEEP = []
def reader(name):
    r = vtk.vtkSTLReader(); r.SetFileName(os.path.join(out, name + ".stl")); r.Update(); return r
def actor(name, color, flat=False):
    r = reader(name); KEEP.append(r)
    n = vtk.vtkPolyDataNormals(); n.SetInputConnection(r.GetOutputPort()); n.SetFeatureAngle(40)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(n.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.GetProperty().SetColor(*color)
    if flat: a.GetProperty().LightingOff()
    else: a.GetProperty().SetAmbient(0.25); a.GetProperty().SetDiffuse(0.8)
    return a
cb = reader("use_card").GetOutput().GetBounds()      # the card, as the case script places it
vb = reader("clam_cover").GetOutput().GetBounds()
def card(side):
    r = vtk.vtkPNGReader(); r.SetFileName(os.path.join(here, "card", "use_card_%s.png" % side))
    t = vtk.vtkTexture(); t.SetInputConnection(r.GetOutputPort()); t.InterpolateOn()
    z = cb[5] + 0.03
    pl = vtk.vtkPlaneSource(); pl.SetOrigin(cb[0], cb[2], z); pl.SetPoint1(cb[1], cb[2], z); pl.SetPoint2(cb[0], cb[3], z)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(pl.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.SetTexture(t); a.GetProperty().LightingOff(); return a
M = 3.0; PX = 14                                      # margin in mm, pixels per mm
x0, x1, y0, y1 = vb[0] - M, vb[1] + M, vb[2] - M, vb[3] + M
W, H = int((x1 - x0) * PX), int((y1 - y0) * PX)
for side in ("front", "back"):
    ren = vtk.vtkRenderer(); ren.SetBackground(27 / 255, 31 / 255, 36 / 255)
    for a in (actor("clam_back", grey), actor("board_standin", board), actor("clam_cover", beige), actor("clam_buttons", dark), actor("paint", black, True), card(side)): ren.AddActor(a)
    w = vtk.vtkRenderWindow(); w.SetOffScreenRendering(1); w.SetSize(W, H); w.AddRenderer(ren)
    c = ren.GetActiveCamera(); c.ParallelProjectionOn(); cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    c.SetFocalPoint(cx, cy, 0); c.SetPosition(cx, cy, 400); c.SetViewUp(0, 1, 0); c.SetParallelScale((y1 - y0) / 2); c.SetClippingRange(1, 800)
    ren.RemoveAllLights()
    for d, k in (((-0.45, 0.55, 1.0), 0.95), ((0.5, -0.3, 1.0), 0.25)):
        L = vtk.vtkLight(); L.SetLightTypeToSceneLight(); L.SetPositional(False); L.SetPosition(cx + d[0] * 300, cy + d[1] * 300, d[2] * 300); L.SetFocalPoint(cx, cy, 0); L.SetIntensity(k); ren.AddLight(L)
    w.Render()
    f = vtk.vtkWindowToImageFilter(); f.SetInput(w); f.Update()
    p = vtk.vtkPNGWriter(); p.SetFileName(os.path.join(out, "demo_%s.png" % side)); p.SetInputConnection(f.GetOutputPort()); p.Write()
B = json.load(open(os.path.join(here, "board_features.json")))
json.dump({"view": [x0, x1, y0, y1], "size": [W, H], "card": cb[:4], "parts": {k: v for k, v in B["parts"].items() if k in ("DISP1", "PB1", "PB5", "D9", "U1", "J10")}},
          open(os.path.join(out, "demo_view.json"), "w"))
print(open(os.path.join(out, "demo_view.json")).read()[:1500])
