# Preview pictures of the case draft (uses VTK, which comes with CadQuery): python enclosure/render_preview.py
import os, vtk
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.join(here, "out")
def actor(name, color, dz=0.0, opacity=1.0):
    r = vtk.vtkSTLReader(); r.SetFileName(os.path.join(out, name + ".stl"))
    n = vtk.vtkPolyDataNormals(); n.SetInputConnection(r.GetOutputPort()); n.SetFeatureAngle(40)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(n.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.GetProperty().SetColor(*color); a.GetProperty().SetOpacity(opacity); a.SetPosition(0, 0, dz)
    return a
def shot(fname, actors, pos, up=(0, 1, 0)):
    ren = vtk.vtkRenderer(); ren.SetBackground(1, 1, 1)
    for a in actors: ren.AddActor(a)
    w = vtk.vtkRenderWindow(); w.SetOffScreenRendering(1); w.SetSize(1200, 900); w.AddRenderer(ren)
    c = ren.GetActiveCamera(); c.SetFocalPoint(50, -50, 0); c.SetPosition(50 + pos[0], -50 + pos[1], pos[2]); c.SetViewUp(*up)
    ren.ResetCamera(); c.Zoom(1.25); w.Render()
    f = vtk.vtkWindowToImageFilter(); f.SetInput(w); f.Update()
    p = vtk.vtkPNGWriter(); p.SetFileName(os.path.join(out, fname)); p.SetInputConnection(f.GetOutputPort()); p.Write()
grey, blue, green = (0.82, 0.82, 0.80), (0.55, 0.66, 0.80), (0.20, 0.50, 0.25)
shot("preview_front.png", [actor("case_back", grey), actor("case_cover", blue)], (60, -120, 220))
shot("preview_exploded.png", [actor("case_back", grey), actor("board_standin", green, 35), actor("case_cover", blue, 80)], (170, -220, 150), (0, 0, 1))
shot("preview_back.png", [actor("case_back", grey), actor("case_cover", blue)], (-60, -120, -220))
