from pathlib import Path
p=Path('studio/selection_transform.hpp');s=p.read_text();s=s.replace('selection::TransformOptions opts;selection::transformObjects(c.document,refs,matrix,opts);return CommandResult::success();','''std::vector<ObjectRef> other;for(auto ref:refs){
            auto frame=[&](graphics::ShapeFrame& f){auto origin=f.centre;f.centre=matrix.apply(origin);f.axisU=matrix.apply(origin+f.axisU)-f.centre;f.axisV=matrix.apply(origin+f.axisV)-f.centre;};
            bool valid=false;Error e;if(ref.kind==ObjectKind::Arrow){graphics::ShapeParams p;graphics::readShape(c.document,ref.id,&p,&valid);if(valid){frame(p.frame);graphics::swapCircleForOval(&p);e=graphics::writeShape(c.document,ref.id,p,c.document.defaultLineWidth());}}
            else if(ref.kind==ObjectKind::Bracket){graphics::BracketParams p;graphics::readBracket(c.document,ref.id,&p,&valid);if(valid){frame(p.frame);e=graphics::writeBracket(c.document,ref.id,p,c.document.defaultLineWidth());}}
            if(!e.ok())return CommandResult::fail(e.code,e.message);if(!valid)other.push_back(ref);
        }selection::TransformOptions opts;selection::transformObjects(c.document,other,matrix,opts);return CommandResult::success();''');p.write_text(s)
